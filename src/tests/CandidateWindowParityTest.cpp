/********************************************************************************
* CandidateWindowParityTest: the candidate window owned by Rust (dime-ui::candwin,
* through CRustCandidateWindow: the model, the DimeCandModel callbacks and the
* scroll bar sync as UIPresenter has them) against the golden files in
* src/tests/golden/CandidateWindow, recorded while the C++ CCandidateWindow
* (removed in phase 6b-3) still matched it side by side. The window exists only
* while its GDI calls are recorded with synthetic text metrics at 96 dpi, so its
* sizes and calls do not depend on the machine's fonts.
*  - PaintMatchesCpp: the GDI calls, layout members, window size, scroll bar.
*  - MessagesMatchCpp: random messages (mouse on rows / the scroll bar / outside,
*    right / middle clicks, wheel, WM_SETCURSOR, WM_VSCROLL, activation,
*    WINDOWPOS, show, WM_PAINT, ...):
*    results, selection, page, callbacks, cursor, capture, mouse tracking,
*    scroll bar state / visibility, shadow visibility, layout members.
*  - OperationsMatchCpp: _Show / _Move / _ResizeWindow / _SetCandStringLength /
*    colors / fill / _SetScrollInfo / scroll bar shifts / selection and paging:
*    window and child rectangles, visibility, state. (_GetWindowExtent depends
*    on the screen's work area and is left out.)
*
* The random draws are the ones the recordings were made with: do not reorder
* them (DIME_GOLDEN_RECORD=1 re-records after a deliberate change).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../Config.h"
#include "RustCandidateWindow.h"
#include "../BaseStructure.h"
#include "../Define.h"
#include "WindowClassTestHelper.h"
#include "Golden.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
namespace
{
    struct Recorder { std::vector<int> actions; };

    HRESULT RecordAction(void* pv, enum CANDWND_ACTION action)
    {
        static_cast<Recorder*>(pv)->actions.push_back(action == CANDWND_ACTION::CAND_ITEM_SELECT ? 0 : 1);
        return S_OK;
    }

    // The Rust window through CRustCandidateWindow, as UIPresenter holds it (the model, the
    // DimeCandModel callbacks and the scroll bar sync are the production ones).
    struct RustSide
    {
        CCandidateRange range;
        std::vector<CCandidateListItem> items;
        Recorder rec;
        std::unique_ptr<CRustCandidateWindow> w;
        DimeCandidate* c = nullptr;
        CCandidateListModel* model = nullptr;

        bool Create(UINT wndWidth, const DimeCandColors& colors, COLORREF fill)
        {
            w = std::make_unique<CRustCandidateWindow>(RecordAction, &rec, &range, FALSE);
            w->_SetClassesForTest(RustCandidateTestClasses());
            if (!w->_Create(wndWidth, 12, nullptr)) return false;
            c = w->_GetRustWindow();
            model = w->_GetModel();
            w->_SetNumberColor(colors.number, colors.number_bk);
            w->_SetTextColor(colors.text, colors.bk);
            w->_SetSelectedTextColor(colors.selected_text, colors.selected_bk);
            w->_SetFillColor(fill);
            return true;
        }
        HWND Wnd() const { return w->_GetWnd(); }
        ~RustSide() { if (w) w->_Destroy(); }
    };

    std::wstring R(const RECT& r)
    {
        return L"(" + std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"," + std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom) + L")";
    }
    RECT Rel(HWND h, const RECT& owner)
    {
        RECT r = { 0, 0, 0, 0 };
        if (h) GetWindowRect(h, &r);
        return { r.left - owner.left, r.top - owner.top, r.right - owner.left, r.bottom - owner.top };
    }

    // Everything observable of the window.
    std::wstring ObserveRust(RustSide& rs)
    {
        DimeCandidate* c = rs.c;
        HWND h = rs.Wnd();
        RECT wr = { 0, 0, 0, 0 };
        GetWindowRect(h, &wr);
        int32_t st[4] = {};
        dime_ui_candidate_layout_state(c, 0, st);
        DimeSbSnapshot ss = {};
        DimeRect hl = {};
        int32_t radius = 0;
        uint32_t color = 0;
        bool hasSb = dime_ui_candidate_sb_state(c, &ss, &hl, &radius, &color) != 0;
        HWND sb = (HWND)dime_ui_candidate_child(c, 0);
        HWND shadow = (HWND)dime_ui_candidate_child(c, 1);
        int page = -1;
        rs.model->GetCurrentPage(&page);
        std::wstring a;
        for (int x : rs.rec.actions) a += std::to_wstring(x);
        return L"rect=" + R(wr) + L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" layout=" + std::to_wstring(st[0]) + L"," + std::to_wstring(st[1]) + L"," + std::to_wstring(st[2]) + L"," + std::to_wstring(st[3]) +
            L" width=" + std::to_wstring(dime_ui_candidate_width(c)) +
            L" sel=" + std::to_wstring(rs.model->Selection()) + L" page=" + std::to_wstring(page) + L" actions=" + a +
            L" capture=" + std::to_wstring(GetCapture() == h) + L" tracking=" + std::to_wstring(dime_ui_candidate_child(c, 2) != nullptr) +
            L" sb=" + R(Rel(sb, wr)) + L"/" + std::to_wstring(ss.si.max) + L"/" + std::to_wstring(ss.si.page) + L"/" + std::to_wstring(ss.si.pos) +
            L" sbEnabled=" + std::to_wstring(hasSb ? (IsWindowEnabled(sb) ? 1 : 0) : -1) + L" sbVisible=" + std::to_wstring(hasSb ? IsWindowVisible(sb) : -1) +
            L" sbHover=" + std::to_wstring(ss.hovered) + L" sbDrag=" + std::to_wstring(ss.dragging) + L" sbAlpha=" + std::to_wstring(ss.alpha) +
            L" shadow=" + R(Rel(shadow, wr)) + L"/" + std::to_wstring(shadow ? IsWindowVisible(shadow) : -1);
    }

    // Runs `paint` on a scratch bitmap of cx x cy (the calls are what is recorded).
    void PaintInto(int cx, int cy, const std::function<void(HDC)>& paint)
    {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cx, cy);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        paint(mem);
        GdiFlush();
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }

    // Golden recordings: 96 dpi and a light system theme whatever the machine
    // (CConfig::GetDpiForHwnd / IsSystemDarkTheme).
    struct Dpi96
    {
        UINT saved = CConfig::testDpiOverride;
        int savedDark = CConfig::testSystemDarkOverride;
        Dpi96() { CConfig::testDpiOverride = 96; CConfig::testSystemDarkOverride = 0; }
        ~Dpi96() { CConfig::testDpiOverride = saved; CConfig::testSystemDarkOverride = savedDark; }
    };

    // The window with its colours (random) and its list, recorded.
    void Setup(RustSide& g, std::mt19937& rng, int rowsInPage, UINT wndWidth, const std::vector<std::wstring>& texts,
        std::vector<std::wstring>& calls)
    {
        DimeCandColors colors = { (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF),
                                  (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF), (uint32_t)(rng() & 0xFFFFFF) };
        COLORREF fill = (COLORREF)(rng() & 0xFFFFFF);
        Golden::GdiRecording rec;
        const wchar_t keys[] = L"1234567890";
        for (int r = 0; r < rowsInPage; r++) g.range.Append()->CandIndex = keys[r % 10];
        Assert::IsTrue(g.Create(wndWidth, colors, fill), L"the window");
        UINT count = (UINT)texts.size();
        g.items.resize(count);
        for (UINT i = 0; i < count; i++)
        {
            g.items[i]._ItemString.Set(texts[i].c_str(), texts[i].size());
            g.w->_AddString(&g.items[i], FALSE);
        }
        UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
        std::vector<UINT> pageIndex(pages);
        for (UINT pg = 0; pg < pages; pg++) pageIndex[pg] = pg * rowsInPage;
        if (pages) g.w->_SetPageIndex(pageIndex.data(), pages);
        g.w->_SetScrollInfo((int)count, rowsInPage);
        g.w->_Move(300, 300);
        g.w->_ResizeWindow();
        calls = rec.Take();
    }

    // The cursor by name: handle values of system cursors are the session's.
    std::wstring CursorName(HCURSOR c)
    {
        if (!c) return L"none";
        static const LPCWSTR ids[] = { IDC_ARROW, IDC_HAND, IDC_IBEAM, IDC_SIZENS, IDC_SIZEALL };
        static const wchar_t* names[] = { L"arrow", L"hand", L"ibeam", L"sizens", L"sizeall" };
        for (int i = 0; i < ARRAYSIZE(ids); i++)
            if (c == LoadCursor(nullptr, ids[i])) return names[i];
        return L"other";
    }

    // The window's mouse (dime_ui_set_cursor_pos_for_test): at `pt` in h's client area,
    // whatever the machine's mouse does.
    void SetGoldenMouse(HWND h, int x, int y)
    {
        POINT p = { x, y };
        ClientToScreen(h, &p);
        DimePoint dp = { p.x, p.y };
        dime_ui_set_cursor_pos_for_test(&dp);
    }

    void AddStep(Golden::Chunks& ch, const std::wstring& head, const std::vector<std::wstring>& calls)
    {
        ch.Add(head);
        for (const std::wstring& c : calls) ch.Add(c);
        ch.Next();
    }

    const std::wstring kPool[] = { L"中", L"國", L"美麗", L"台灣大學", L"Wi-Fi", L"一二三四五六七八九十甲乙丙丁戊",
                                   L"abcdefghijklmnopqrstuvwxyz", L"ㄅㄆㄇ" };
}

namespace DIMEUnitTests
{
    TEST_CLASS(CandidateWindowParityTest)
    {
    public:
        TEST_METHOD_INITIALIZE(Init)
        {
            EnsureWindowClassesRegistered();
            savedFont = Global::defaultlFontHandle;
            font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
        }
        TEST_METHOD_CLEANUP(Cleanup)
        {
            ReleaseCapture();
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
        }

        TEST_METHOD(PaintMatchesCpp)
        {
            std::mt19937 rng(81);
            int painted = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"paints", 6);
            for (int s = 0; s < 40; s++)
            {
                int rows = 1 + (int)(rng() % 9);
                UINT width = 1 + rng() % 5;
                std::vector<std::wstring> texts(rng() % 30);
                for (auto& t : texts) t = kPool[rng() % ARRAYSIZE(kPool)];
                RustSide g;
                std::vector<std::wstring> setup;
                Setup(g, rng, rows, width, texts, setup);
                AddStep(ch, L"create " + ObserveRust(g), setup);
                for (int v = 0; v < 3; v++)
                {
                    if (!texts.empty())
                    {
                        int sel = (int)(rng() % texts.size());
                        g.w->_SetSelection(sel, FALSE);
                    }
                    RECT grc = { 0, 0, 0, 0 };
                    GetClientRect(g.Wnd(), &grc);
                    Golden::GdiRecording rec;
                    PaintInto(max(1, (int)grc.right), max(1, (int)grc.bottom), [&](HDC dc) {
                        DimeRect pr = { grc.left, grc.top, grc.right, grc.bottom };
                        dime_ui_candidate_paint(g.c, dc, &pr);
                    });
                    std::vector<std::wstring> calls = rec.Take();
                    AddStep(ch, L"paint " + ObserveRust(g), calls);
                    painted++;
                }
            }
            ch.Finish();
            Golden::Check(L"CandidateWindow", L"Paint", golden);
            Logger::WriteMessage((std::to_wstring(painted) + L" candidate paints").c_str());
        }

        TEST_METHOD(MessagesMatchCpp)
        {
            std::mt19937 rng(82);
            int sent = 0, selects = 0, cancels = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"messages", 25);
            for (int scenario = 0; scenario < 20; scenario++)
            {
                std::vector<std::wstring> texts(rng() % 40);
                for (auto& t : texts) t = kPool[rng() % ARRAYSIZE(kPool)];
                int rows = 1 + (int)(rng() % 9);
                UINT width = 1 + rng() % 4;
                RustSide g;
                std::vector<std::wstring> setup;
                Setup(g, rng, rows, width, texts, setup);
                AddStep(ch, L"create " + ObserveRust(g), setup);
                RECT gclient = { 0, 0, 0, 0 };
                GetClientRect(g.Wnd(), &gclient);
                int gcw = max(1, (int)gclient.right), gch = max(1, (int)gclient.bottom);
                HWND gcapture = nullptr;
                for (int step = 0; step < 100; step++)
                {
                    UINT msg = 0;
                    WPARAM wp = 0;
                    LPARAM lp = 0;
                    bool useWindowPos = false;
                    WINDOWPOS pos = {};
                    uint32_t rx = rng(), ry = rng();
                    int gx = (int)(rx % (gcw + 20)) - 10, gy = (int)(ry % (gch + 20)) - 10;
                    bool xy = false;
                    switch (rng() % 16)
                    {
                    case 0: case 1: msg = WM_MOUSEMOVE; xy = true; break;
                    case 2: msg = WM_LBUTTONDOWN; xy = true; break;
                    case 3: msg = WM_LBUTTONUP; xy = true; break;
                    case 4: msg = WM_MOUSEWHEEL; wp = MAKEWPARAM(0, (rng() % 2) ? WHEEL_DELTA : (WORD)-WHEEL_DELTA); break;
                    case 5: msg = WM_VSCROLL; wp = MAKEWPARAM(rng() % 9, rng() % (texts.size() + 3)); break;
                    case 6: msg = WM_SETCURSOR; lp = MAKELPARAM(HTCLIENT, (rng() % 2) ? WM_MOUSEMOVE : WM_LBUTTONUP); break;
                    case 7:
                    {
                        static const WORD ev[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_MOUSEMOVE };
                        msg = WM_MOUSEACTIVATE; lp = MAKELPARAM(HTCLIENT, ev[rng() % 4]); break;
                    }
                    case 8:
                    {
                        msg = (rng() % 2) ? WM_WINDOWPOSCHANGING : WM_WINDOWPOSCHANGED;
                        useWindowPos = true;
                        static const UINT fl[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_HIDEWINDOW, SWP_SHOWWINDOW, SWP_NOREDRAW };
                        for (UINT b : fl) if (rng() % 3 == 0) pos.flags |= b;
                        break;
                    }
                    case 9: msg = WM_SHOWWINDOW; wp = rng() % 2; break;
                    case 10: msg = WM_ERASEBKGND; break;
                    case 11: msg = 0x02A3 /* WM_MOUSELEAVE */; break;
                    case 12: msg = WM_CTLCOLORSCROLLBAR; break;
                    case 13: msg = 0x024B /* WM_POINTERACTIVATE */; break;
                    case 14: msg = (rng() % 2) ? WM_RBUTTONDOWN : WM_MBUTTONUP; xy = true; break;
                    case 15: msg = WM_PAINT; break;
                    }
                    // Mouse capture is global: give the window the capture it left.
                    if (gcapture) SetCapture(gcapture); else ReleaseCapture();
                    SetCursor(nullptr);
                    WINDOWPOS wpos = pos;
                    wpos.hwnd = g.Wnd();
                    SetGoldenMouse(g.Wnd(), gx, gy);
                    Golden::GdiRecording rec;
                    LRESULT r = SendMessage(g.Wnd(), msg, wp, useWindowPos ? (LPARAM)&wpos : xy ? MAKELPARAM(gx, gy) : lp);
                    std::vector<std::wstring> calls = rec.Take();
                    dime_ui_set_cursor_pos_for_test(nullptr);
                    if (msg == WM_CTLCOLORSCROLLBAR) r = (r != 0);   // the window answers with its own brush
                    AddStep(ch, L"msg " + std::to_wstring(msg) + L" " + std::to_wstring((uint32_t)wp) + L" " + std::to_wstring((uint32_t)(xy ? MAKELPARAM(gx, gy) : useWindowPos ? 0 : lp)) +
                        L" " + ObserveRust(g) + L" result=" + std::to_wstring(r) + L" cursor=" + CursorName(GetCursor()) +
                        L" wp=" + std::to_wstring(useWindowPos ? wpos.flags : 0), calls);
                    gcapture = GetCapture();
                    ReleaseCapture();
                    sent++;
                }
                for (int a : g.rec.actions) (a == 0 ? selects : cancels)++;
            }
            ch.Finish();
            Golden::Check(L"CandidateWindow", L"Messages", golden);
            Logger::WriteMessage((std::to_wstring(sent) + L" candidate messages, " + std::to_wstring(selects) + L" selections, " +
                std::to_wstring(cancels) + L" cancels").c_str());
            Assert::IsTrue(selects > 0 && cancels > 0, L"coverage");
        }

        TEST_METHOD(OperationsMatchCpp)
        {
            std::mt19937 rng(83);
            int done = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"operations", 30);
            for (int scenario = 0; scenario < 20; scenario++)
            {
                std::vector<std::wstring> texts(rng() % 40);
                for (auto& t : texts) t = kPool[rng() % ARRAYSIZE(kPool)];
                int rows = 1 + (int)(rng() % 9);
                UINT width = 1 + rng() % 4;
                RustSide g;
                std::vector<std::wstring> setup;
                Setup(g, rng, rows, width, texts, setup);
                AddStep(ch, L"create " + ObserveRust(g), setup);
                for (int step = 0; step < 60; step++)
                {
                    int op = (int)(rng() % 14);
                    BOOL flag = rng() % 2;
                    int x = (int)(rng() % 1200) - 100, y = (int)(rng() % 800) - 100;
                    UINT w = 1 + rng() % 8;
                    COLORREF c1 = rng() & 0xFFFFFF, c2 = rng() & 0xFFFFFF;
                    int n = (int)(rng() % 7) - 3;
                    int index = (int)(rng() % (texts.size() + 3)) - 1;
                    // The text extent's draws (for _GetWindowExtent, which is not recorded:
                    // it depends on the screen's work area).
                    (void)rng();
                    (void)rng();

                    ReleaseCapture();   // capture is global; _Show(TRUE) takes it
                    CRustCandidateWindow* rw = g.w.get();
                    std::wstring more;
                    Golden::GdiRecording rec;
                    switch (op)
                    {
                    case 0: rw->_Show(flag); break;
                    case 1: rw->_Move(x, y); break;
                    case 2: rw->_ResizeWindow(); break;
                    case 3: rw->_SetCandStringLength(w); break;
                    case 4: rw->_SetTextColor(c1, c2); rw->_SetFillColor(c2); break;
                    case 5: rw->_SetScrollInfo((int)texts.size() + n, 3 + (n & 3)); break;
                    case 6: dime_ui_candidate_op(g.c, 6, n, flag); break;
                    case 7: dime_ui_candidate_op(g.c, 7, n, flag); break;
                    case 8: dime_ui_candidate_op(g.c, 8, x & 63, flag); break;
                    case 9: rw->_InvalidateRect(); break;
                    case 10:
                    {
                        RECT cand = { 0, 0, 0, 0 };
                        rw->_GetClientRect(&cand);
                        more = L" client=" + R(cand);
                        break;
                    }
                    // UIPresenter's selection and paging: the model, then the scroll bar.
                    case 11: more = L" ret=" + std::to_wstring(rw->_MoveSelection(n, flag)); break;
                    case 12: more = L" ret=" + std::to_wstring(rw->_SetSelection(index, flag)); break;
                    case 13: more = L" ret=" + std::to_wstring(rw->_MovePage(n, flag)); break;
                    }
                    std::vector<std::wstring> calls = rec.Take();
                    AddStep(ch, L"op " + std::to_wstring(op) + L" " + ObserveRust(g) + more, calls);
                    ReleaseCapture();
                    done++;
                }
            }
            ch.Finish();
            Golden::Check(L"CandidateWindow", L"Operations", golden);
            Logger::WriteMessage((std::to_wstring(done) + L" candidate operations").c_str());
        }

    private:
        HFONT savedFont = nullptr;
        HFONT font = nullptr;
    };
}
#endif

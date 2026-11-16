/********************************************************************************
* ScrollBarProcParityTest: the candidate scroll bar's events in dime-ui
* (sbproc / sbwin) against the golden file src/tests/golden/ScrollBarProc,
* recorded while the C++ CScrollBarWindow (removed in phase 6b-3) still matched
* it side by side. A Rust candidate window with its own Rust scroll bar, driven
* through dime_ui_candidate_sb_op only while its GDI calls are recorded with
* synthetic text metrics at 96 dpi: scroll info, _SetCurPos with every
* notification code, _ShiftLine / _ShiftPage / _ShiftPosition, clicks on the
* buttons / track / thumb, drags, button-up, hover, _SetCandidateMouseIn, the
* repeat and collapse timers, _OnScrollActivity, _Show / _Enable, owner moves,
* WM_MOUSELEAVE / WM_ERASEBKGND / WM_PAINT / WM_TIMER. After each step: the
* whole scroll bar state, enabled / visible / window rectangle / layered alpha,
* the mouse capture, the timers started and stopped, and the candidate window's
* selection and page (the scroll bar notifies it with WM_VSCROLL).
*
* The random draws are the ones the recording was made with: do not reorder
* them (DIME_GOLDEN_RECORD=1 re-records after a deliberate change).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../BaseStructure.h"
#include "../UIConstants.h"
#include "WindowClassTestHelper.h"
#include "RustCandidateWindow.h"
#include "Golden.h"
#include "../Config.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }

    // The scroll bar's own timers (the button repeat and the fade).
    const UINT_PTR kButtonRepeatTimerId = 39779;
    const UINT_PTR kFadeTimerId = 39780;

    struct TimerEvent
    {
        BOOL start;
        UINT elapse;
        UINT_PTR id;
    };

    // Golden recordings: 96 dpi and a light system theme whatever the machine
    // (CConfig::GetDpiForHwnd / IsSystemDarkTheme).
    struct Dpi96
    {
        UINT saved = CConfig::testDpiOverride;
        int savedDark = CConfig::testSystemDarkOverride;
        Dpi96() { CConfig::testDpiOverride = 96; CConfig::testSystemDarkOverride = 0; }
        ~Dpi96() { CConfig::testDpiOverride = saved; CConfig::testSystemDarkOverride = savedDark; }
    };

    void GoldenTimerHook(void* ctx, int32_t start, uint32_t elapse, uintptr_t id)
    {
        static_cast<std::vector<TimerEvent>*>(ctx)->push_back({ start, elapse, id });
    }

    struct GoldenSide
    {
        CCandidateRange range;
        std::vector<CCandidateListItem> items;
        std::unique_ptr<CRustCandidateWindow> w;
        std::vector<TimerEvent> timers;
        DimeCandidate* C() const { return w->_GetRustWindow(); }
        HWND Sb() const { return (HWND)dime_ui_candidate_child(C(), 0); }
        ~GoldenSide()
        {
            if (w) { dime_ui_candidate_set_timer_hook(C(), nullptr, nullptr); w->_Destroy(); }
        }
    };

    std::wstring GoldenSnapshot(GoldenSide& g)
    {
        DimeSbSnapshot st = {};
        DimeRect hl = {};
        int32_t radius = 0;
        uint32_t color = 0;
        dime_ui_candidate_sb_state(g.C(), &st, &hl, &radius, &color);
        HWND h = g.Sb();
        RECT rc = { 0, 0, 0, 0 };
        GetWindowRect(h, &rc);
        RECT pr = { 0, 0, 0, 0 };
        GetWindowRect(g.w->_GetWnd(), &pr);
        BYTE alpha = 0;
        COLORREF key = 0;
        DWORD flags = 0;
        GetLayeredWindowAttributes(h, &key, &alpha, &flags);
        int page = -1;
        g.w->_GetCurrentPage(&page);
        std::wstring t;
        for (const TimerEvent& e : g.timers)
            t += (e.start ? L"+" : L"-") + std::to_wstring(e.id) + (e.start ? L"@" + std::to_wstring(e.elapse) : L"") + L" ";
        return L"si=" + std::to_wstring(st.si.max) + L"/" + std::to_wstring(st.si.page) + L"/" + std::to_wstring(st.si.pos) +
            L" drag=" + std::to_wstring(st.dragging) + L"," + std::to_wstring(st.drag_start_y) + L"," + std::to_wstring(st.drag_start_pos) +
            L" alpha=" + std::to_wstring(st.alpha) + L" fading=" + std::to_wstring(st.fading) + L" hovered=" + std::to_wstring(st.hovered) +
            L" tracking=" + std::to_wstring(st.tracking) + L" held=" + std::to_wstring(st.held) + L" repeat=" + std::to_wstring(st.repeat_started) +
            L" enabled=" + std::to_wstring(IsWindowEnabled(h)) + L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" rect=" + std::to_wstring(rc.left - pr.left) + L"," + std::to_wstring(rc.top - pr.top) + L"," + std::to_wstring(rc.right - rc.left) +
            L"x" + std::to_wstring(rc.bottom - rc.top) + L" layeredAlpha=" + std::to_wstring(alpha) +
            L" capture=" + std::to_wstring(GetCapture() == g.w->_GetWnd()) +
            L" sel=" + std::to_wstring(g.w->_GetSelection()) + L" page=" + std::to_wstring(page) +
            L" highlight=" + std::to_wstring(hl.left) + L"," + std::to_wstring(hl.top) + L"," + std::to_wstring(hl.right) + L"," +
            std::to_wstring(hl.bottom) + L"/" + std::to_wstring(radius) + L" timers=" + t;
    }

    // The scroll bar's mouse (dime_ui_set_cursor_pos_for_test): at `pt` in h's client area,
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
}

namespace DIMEUnitTests
{
    TEST_CLASS(ScrollBarProcParityTest)
    {
    public:
        TEST_METHOD(EventsMatchCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;

            std::mt19937 rng(53);
            int steps = 0, notified = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"steps", 30);
            for (int scenario = 0; scenario < 20; scenario++)
            {
                int rowsInPage = 1 + (int)(rng() % 9);
                UINT count = rng() % 60;
                bool shown = scenario % 2 == 0;

                GoldenSide g;
                {
                    Golden::GdiRecording rec;
                    for (int r = 0; r < rowsInPage; r++) g.range.Append()->CandIndex = L'1' + (wchar_t)r;
                    g.w = std::make_unique<CRustCandidateWindow>(NoCallback, nullptr, &g.range, FALSE);
                    g.w->_SetClassesForTest(RustCandidateTestClasses());
                    Assert::IsTrue(g.w->_Create(2, 12, nullptr) != FALSE, L"the window");
                    dime_ui_candidate_set_timer_hook(g.C(), GoldenTimerHook, &g.timers);
                    g.items.resize(count);
                    for (UINT i = 0; i < count; i++)
                    {
                        g.items[i]._ItemString.Set(L"中", 1);
                        g.w->_AddString(&g.items[i], FALSE);
                    }
                    UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
                    std::vector<UINT> pageIndex(pages);
                    for (UINT pg = 0; pg < pages; pg++) pageIndex[pg] = pg * rowsInPage;
                    if (pages) g.w->_SetPageIndex(pageIndex.data(), pages);
                    g.w->_Move(400, 300);
                    g.w->_ResizeWindow();
                    // _SetScrollInfo as the presenter does.
                    g.w->_SetScrollInfo(count, rowsInPage);
                    if (shown) g.w->_Show(TRUE);
                    ReleaseCapture();   // _Show(TRUE) captures the mouse; start without it
                    std::vector<std::wstring> calls = rec.Take();
                    AddStep(ch, L"create " + GoldenSnapshot(g), calls);
                }
                Assert::IsNotNull(g.Sb(), L"the scroll bar");

                HWND gcapture = nullptr;
                RECT gClient = { 0, 0, 0, 0 };
                GetClientRect(g.Sb(), &gClient);
                int gsw = max(1, (int)gClient.right), gsh = max(1, (int)gClient.bottom);

                for (int step = 0; step < 150; step++)
                {
                    int op = (int)(rng() % 21);
                    int a = (int)(rng() % (count + 10)) - 5, b = (int)(rng() % 8) - 1;
                    uint32_t rx = rng(), ry = rng();
                    POINT gpt = { (LONG)(rx % (gsw + 6)) - 3, (LONG)(ry % (gsh + 10)) - 5 };
                    DimeScrollInfo si;
                    si.max = (int)(rng() % 60); si.page = 1 + (int)(rng() % 10); si.pos = (int)(rng() % 70) - 5;
                    UINT_PTR timers[] = { kButtonRepeatTimerId, UI::SCROLLBAR_COLLAPSE_TIMER_ID, kFadeTimerId, 12345 };
                    UINT_PTR timer = timers[rng() % 4];
                    BOOL flag = rng() % 2;
                    bool setInfo = rng() % 4 == 0;
                    int radius = (int)(rng() % 3) - 1;
                    UINT paintMsg = (rng() % 2) ? WM_ERASEBKGND : WM_PAINT;

                    DimeCandidate* c = g.C();
                    HWND gsb = g.Sb();
                    // Mouse capture is global: give the window the capture it left.
                    if (gcapture) SetCapture(gcapture); else ReleaseCapture();
                    SetGoldenMouse(gsb, gpt.x, gpt.y);
                    Golden::GdiRecording rec;
                    switch (op)
                    {
                    case 0: if (setInfo) dime_ui_candidate_sb_set(c, &si, nullptr, 0, 0); break;
                    case 1: dime_ui_candidate_sb_op(c, 12, a, b); break;            // _SetCurPos
                    case 2: dime_ui_candidate_sb_op(c, 1, b - 2, flag); break;      // _ShiftLine
                    case 3: dime_ui_candidate_sb_op(c, 2, b - 2, flag); break;      // _ShiftPage
                    case 4: dime_ui_candidate_sb_op(c, 3, a, flag); break;          // _ShiftPosition
                    case 5: case 6: dime_ui_candidate_sb_op(c, 7, gpt.x, gpt.y); break;   // _OnLButtonDown
                    case 7: dime_ui_candidate_sb_op(c, 8, 0, 0); break;             // _OnLButtonUp
                    case 8: case 9: dime_ui_candidate_sb_op(c, 9, 0, 0); break;     // _OnMouseMove
                    case 10: dime_ui_candidate_sb_op(c, 4, flag, 0); break;         // _SetCandidateMouseIn
                    case 11: dime_ui_candidate_sb_op(c, 10, (int32_t)timer, 0); break;   // _OnTimerID
                    case 12: dime_ui_candidate_sb_op(c, 5, 0, 0); break;            // _OnScrollActivity
                    case 13: dime_ui_candidate_sb_op(c, 0, flag, 0); break;         // _Show
                    case 14: dime_ui_candidate_sb_op(c, 13, flag, 0); break;        // _Enable
                    case 15: dime_ui_candidate_sb_op(c, 6, 0, 0); break;            // _OnOwnerWndMoved
                    case 16: SendMessage(gsb, 0x02A3 /* WM_MOUSELEAVE */, 0, 0); break;
                    case 17: SendMessage(gsb, paintMsg, 0, 0); break;
                    case 18: SendMessage(gsb, WM_TIMER, timer, 0); break;
                    case 19: { DimeRect hr = { 0, 0, 5, 5 }; dime_ui_candidate_sb_set(c, nullptr, &hr, radius, 0); break; }
                    // The timer as Windows delivers it: to the candidate window.
                    case 20: SendMessage(g.w->_GetWnd(), WM_TIMER, timer, 0); break;
                    }
                    std::vector<std::wstring> calls = rec.Take();
                    dime_ui_set_cursor_pos_for_test(nullptr);
                    AddStep(ch, L"op " + std::to_wstring(op) + L" " + GoldenSnapshot(g), calls);
                    gcapture = GetCapture();
                    ReleaseCapture();
                    if (g.w->_GetSelection() != 0) notified++;
                    steps++;
                }
                ReleaseCapture();
            }
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            ch.Finish();
            Golden::Check(L"ScrollBarProc", L"Events", golden);
            Logger::WriteMessage((std::to_wstring(steps) + L" scroll bar steps, " + std::to_wstring(notified) +
                L" with the candidate selection moved by the scroll bar").c_str());
            Assert::IsTrue(notified > 0, L"coverage");
        }
    };
}
#endif

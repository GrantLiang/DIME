/********************************************************************************
* CandidatePaintParityTest: the candidate window drawing of dime-ui (candwin /
* candpaint, through CRustCandidateWindow) against the golden files in
* src/tests/golden/CandidatePaint, recorded while the C++ original
* (CCandidateWindow::_OnPaint, removed in phase 6b-3) still matched it pixel by
* pixel. A window created and painted only while the GDI calls are recorded
* with synthetic text metrics at 96 dpi: the calls, the layout members painting
* updates (cxTitle, cyRow, padding, wndWidth), the window size (_ResizeWindow)
* and the scroll bar's selection highlight. Scenarios: several fonts and sizes,
* candidate widths, page sizes, item lengths (truncation and widening),
* selections on every page, colors, missing items. The scroll bar:
* dime_ui_sb_paint for random states (ScrollBarPaintMatchesCpp).
*
* The random draws are the ones the recordings were made with: do not reorder
* them (DIME_GOLDEN_RECORD=1 re-records after a deliberate change).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "RustCandidateWindow.h"
#include "../BaseStructure.h"
#include "../Config.h"
#include "WindowClassTestHelper.h"
#include "Golden.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;


namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }

    // CCandidateWindow's layout members (_cxTitle, _cyRow, _padding, _wndWidth).
    struct LayoutState
    {
        int cxTitle;
        int cyRow;
        int padding;
        UINT wndWidth;
    };

    struct PaintResult
    {
        LayoutState state = {};
        RECT window = {};
        RECT highlight = {};
        int highlightRadius = 0;
        COLORREF highlightColor = 0;
    };

    // Paint like WM_PAINT does (off-screen, rcPaint = client).
    PaintResult Paint(CRustCandidateWindow& w, const LayoutState& start)
    {
        DimeCandidate* c = w._GetRustWindow();
        int32_t st[4] = { start.cxTitle, start.cyRow, start.padding, (int32_t)start.wndWidth };
        dime_ui_candidate_layout_state(c, 1, st);
        w._ResizeWindow();

        RECT rc = { 0, 0, 0, 0 };
        GetClientRect(w._GetWnd(), &rc);
        int cx = max(1, (int)(rc.right - rc.left)), cy = max(1, (int)(rc.bottom - rc.top));

        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cx, cy);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);

        DimeRect pr = { rc.left, rc.top, rc.right, rc.bottom };
        dime_ui_candidate_paint(c, mem, &pr);
        GdiFlush();

        PaintResult r;
        GetWindowRect(w._GetWnd(), &r.window);
        int32_t after[4] = {};
        dime_ui_candidate_layout_state(c, 0, after);
        r.state = { after[0], after[1], after[2], (UINT)after[3] };
        DimeSbSnapshot ss = {};
        DimeRect hl = {};
        int32_t radius = 0;
        uint32_t color = 0;
        if (dime_ui_candidate_sb_state(c, &ss, &hl, &radius, &color))
        {
            r.highlight = { hl.left, hl.top, hl.right, hl.bottom };
            r.highlightRadius = radius;
            r.highlightColor = color;
        }

        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return r;
    }

    std::wstring Describe(const LayoutState& s)
    {
        return L"cxTitle=" + std::to_wstring(s.cxTitle) + L" cyRow=" + std::to_wstring(s.cyRow) +
            L" padding=" + std::to_wstring(s.padding) + L" wndWidth=" + std::to_wstring(s.wndWidth);
    }

    // The golden record of a paint: what Paint returned except the window's
    // position, and the GDI calls.
    void AddGolden(Golden::Chunks& ch, const std::wstring& where, const PaintResult& g, const std::vector<std::wstring>& calls)
    {
        ch.Add(where + L" " + Describe(g.state) + L" size " + std::to_wstring(g.window.right - g.window.left) + L"x" +
            std::to_wstring(g.window.bottom - g.window.top) + L" highlight " + std::to_wstring(g.highlight.left) + L"," +
            std::to_wstring(g.highlight.top) + L"," + std::to_wstring(g.highlight.right) + L"," + std::to_wstring(g.highlight.bottom) +
            L" r=" + std::to_wstring(g.highlightRadius) + L" c=" + std::to_wstring(g.highlightColor));
        for (const std::wstring& c : calls) ch.Add(c);
        ch.Next();
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
}

namespace DIMEUnitTests
{
    TEST_CLASS(CandidatePaintParityTest)
    {
    public:
        TEST_METHOD(PaintMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;

            const wchar_t* faces[] = { L"Microsoft JhengHei", L"PMingLiU", L"Segoe UI" };
            const int heights[] = { -16, -24, -37 };
            // A pool of candidate strings (CJK, ASCII, long ones that get truncated / widen the window).
            const std::wstring pool[] = {
                L"中", L"國", L"美麗", L"台灣大學", L"a", L"Wi-Fi", L"…", L"ㄅㄆㄇ",
                L"一二三四五六七八九十甲乙丙", L"一二三四五六七八九十甲乙丙丁戊己庚辛",
                L"abcdefghijklmnopqrstuvwxyz", L"長長長長長長長長長長長長長長長長長長長長長長長",
                L"x　y", L"\U00020000\U00020001", L"ab", L"龜鱉",
            };

            std::mt19937 rng(31);
            int scenarios = 0, painted = 0, withIndicator = 0, truncatedOrWidened = 0;
            Dpi96 dpi96;
            // The window exists and paints only while the GDI calls are recorded
            // with synthetic text metrics, so its sizes and calls do not depend
            // on the machine's fonts.
            Golden::Log golden;
            Golden::Chunks ch(golden, L"paints", 4);
            for (const wchar_t* face : faces)
            for (int height : heights)
            {
                HFONT font = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, face);
                Assert::IsNotNull(font);
                Global::defaultlFontHandle = font;

                for (int s = 0; s < 12; s++)
                {
                    int rowsInPage = 1 + (int)(rng() % 9);
                    UINT wndWidth = 1 + rng() % 6;
                    UINT count = rng() % 30;

                    CCandidateRange range;
                    const wchar_t keys[] = L"1234567890";
                    for (int k = 0; k < rowsInPage; k++)
                        range.Append()->CandIndex = keys[k % 10];

                    std::vector<std::wstring> setup;
                    CRustCandidateWindow gw(NoCallback, nullptr, &range, FALSE);
                    {
                        Golden::GdiRecording rec;
                        gw._SetClassesForTest(RustCandidateTestClasses());
                        gw._Create(wndWidth, 12, nullptr);
                        gw._SetCandStringLength(wndWidth);
                        setup = rec.Take();
                    }
                    Assert::IsNotNull(gw._GetWnd(), L"a real window");

                    std::vector<CCandidateListItem> items(count);
                    for (UINT i = 0; i < count; i++)
                    {
                        const std::wstring& t = pool[rng() % ARRAYSIZE(pool)];
                        items[i]._ItemString.Set(t.c_str(), t.size());
                        gw._AddString(&items[i], FALSE);
                    }
                    UINT pages = count ? (count + rowsInPage - 1) / rowsInPage : 0;
                    std::vector<UINT> pageIndex(pages);
                    for (UINT p = 0; p < pages; p++) pageIndex[p] = p * rowsInPage;
                    if (pages) gw._SetPageIndex(pageIndex.data(), pages);

                    COLORREF colors[7];
                    for (COLORREF& cc : colors) cc = (COLORREF)(rng() & 0xFFFFFF);
                    {
                        Golden::GdiRecording rec;
                        gw._SetScrollInfo(count, rowsInPage);
                        gw._SetNumberColor(colors[0], colors[1]);
                        gw._SetTextColor(colors[2], colors[3]);
                        gw._SetSelectedTextColor(colors[4], colors[5]);
                        gw._SetFillColor(colors[6]);
                        std::vector<std::wstring> more = rec.Take();
                        setup.insert(setup.end(), more.begin(), more.end());
                    }

                    // A few selections per window: first item, random ones (any page), none.
                    for (int sel = 0; sel < 4; sel++)
                    {
                        if (count)
                        {
                            int index = sel == 0 ? 0 : (int)(rng() % count);
                            gw._SetSelection(index, FALSE);
                        }
                        LayoutState start = { (int)(rng() % 300), 20, 10, wndWidth };

                        std::wstring where = std::wstring(face) + L" " + std::to_wstring(height) + L" rows=" + std::to_wstring(rowsInPage) +
                            L" count=" + std::to_wstring(count) + L" width=" + std::to_wstring(wndWidth) + L" sel=" + std::to_wstring(gw._GetSelection());
                        Golden::GdiRecording rec;
                        PaintResult g = Paint(gw, start);
                        std::vector<std::wstring> calls = rec.Take();
                        if (sel == 0) calls.insert(calls.begin(), setup.begin(), setup.end());
                        AddGolden(ch, where, g, calls);

                        if (pages > 1) withIndicator++;
                        if (g.state.wndWidth != wndWidth) truncatedOrWidened++;
                        painted++;
                    }
                    gw._Destroy();
                    scenarios++;
                }
                Global::defaultlFontHandle = savedFont;
                DeleteObject(font);
            }
            ch.Finish();
            Golden::Check(L"CandidatePaint", L"Paint", golden);
            Logger::WriteMessage((std::to_wstring(scenarios) + L" windows, " + std::to_wstring(painted) + L" paints, " +
                std::to_wstring(withIndicator) + L" with a page indicator, " + std::to_wstring(truncatedOrWidened) + L" widened").c_str());
            Assert::IsTrue(withIndicator > 0 && truncatedOrWidened > 0, L"coverage");
        }

        // The scroll bar painting (dime_ui_sb_paint, what the Rust scroll bar window
        // paints with) on a 16 x 200 scroll bar: background (color key / the candidate's
        // brush), hovered arrows + pill thumb or the idle line, the RC26 highlight;
        // light / dark, layered / not, scroll ranges including "everything fits".
        TEST_METHOD(ScrollBarPaintMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;

            std::mt19937 rng(37);
            int painted = 0, layeredPaints = 0, plainPaints = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"scroll bars", 16);
            for (int s = 0; s < 20; s++)
            {
                int rowsInPage = 1 + (int)(rng() % 9);
                UINT width = 1 + rng() % 4;
                COLORREF fill = (COLORREF)(rng() & 0xFFFFFF);
                // The scroll bar of a candidate window: its layered style and its
                // candidate window's fill decide the background.
                CCandidateRange range;
                for (int r = 0; r < rowsInPage; r++) range.Append()->CandIndex = L'1' + (wchar_t)r;
                CRustCandidateWindow w(NoCallback, nullptr, &range, FALSE);
                w._SetClassesForTest(RustCandidateTestClasses());
                Assert::IsTrue(w._Create(width, 12, nullptr) != FALSE);
                w._SetFillColor(fill);
                w._ResizeWindow();
                HWND sbh = (HWND)dime_ui_candidate_child(w._GetRustWindow(), 0);
                Assert::IsNotNull(sbh, L"the scroll bar");
                LONG style = GetWindowLong(sbh, GWL_EXSTYLE);

                for (int v = 0; v < 16; v++)
                {
                    DimeScrollInfo si;
                    si.max = (int)(rng() % 60);
                    si.page = 1 + (int)(rng() % 12);
                    si.pos = (int)(rng() % 70);
                    BOOL hovered = rng() % 2;
                    CConfig::SetColorMode((rng() % 2) ? IME_COLOR_MODE::IME_COLOR_MODE_DARK : IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
                    RECT hl = { -(LONG)(rng() % 200), (LONG)(rng() % 100), (LONG)(rng() % 40), 0 };
                    hl.bottom = hl.top + 10 + (LONG)(rng() % 30);
                    int radius = (rng() % 3) ? (int)(rng() % 9) : -1;
                    COLORREF hlColor = (COLORREF)(rng() & 0xFFFFFF);
                    bool layered = rng() % 2;
                    (layered ? layeredPaints : plainPaints)++;
                    bool withPaintStruct = (v != 0);

                    SetWindowLong(sbh, GWL_EXSTYLE, layered ? (style | WS_EX_LAYERED) : (style & ~WS_EX_LAYERED));
                    Assert::AreEqual(layered, (GetWindowLong(sbh, GWL_EXSTYLE) & WS_EX_LAYERED) != 0, L"layered style");

                    HDC screen = GetDC(nullptr);
                    HDC mem = CreateCompatibleDC(screen);
                    HBITMAP bmp = CreateCompatibleBitmap(screen, 16, 200);
                    HGDIOBJ oldBmp = SelectObject(mem, bmp);
                    DimeSbPaint in = {};
                    in.hwnd = sbh;
                    in.has_paint = withPaintStruct;
                    in.paint = { 0, 0, 16, 200 };
                    in.client = { 0, 0, 16, 200 };
                    in.dpi = 96;
                    in.si = si;
                    in.dark = CConfig::GetEffectiveDarkMode() ? 1 : 0;
                    in.full = hovered;
                    in.highlight = { hl.left, hl.top, hl.right, hl.bottom };
                    in.highlight_radius = radius;
                    in.highlight_color = hlColor;
                    Golden::GdiRecording rec;
                    dime_ui_sb_paint(mem, &in);
                    std::vector<std::wstring> calls = rec.Take();
                    ch.Add(L"max=" + std::to_wstring(si.max) + L" page=" + std::to_wstring(si.page) + L" pos=" + std::to_wstring(si.pos) +
                        L" hovered=" + std::to_wstring(hovered) + L" dark=" + std::to_wstring(in.dark) + L" layered=" + std::to_wstring(layered));
                    for (const std::wstring& c : calls) ch.Add(c);
                    ch.Next();
                    SelectObject(mem, oldBmp);
                    DeleteObject(bmp);
                    DeleteDC(mem);
                    ReleaseDC(nullptr, screen);
                    painted++;
                }
                SetWindowLong(sbh, GWL_EXSTYLE, style);
                w._Destroy();
            }
            CConfig::SetColorMode(savedMode);
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            ch.Finish();
            Golden::Check(L"CandidatePaint", L"ScrollBar", golden);
            Logger::WriteMessage((std::to_wstring(painted) + L" scroll bar paints (" + std::to_wstring(layeredPaints) +
                L" layered, " + std::to_wstring(plainPaints) + L" not)").c_str());
            Assert::IsTrue(layeredPaints > 0 && plainPaints > 0, L"coverage");
        }
    };
}

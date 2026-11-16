/********************************************************************************
* SettingsWindowPaintParityTest: the settings window painters, C++
* (SettingsWindowPaint::Cpp, lifted out of SettingsWindow.cpp) vs Rust
* (dime-settings paint.rs via dime_settings_paint_*). Each case paints into
* two 32-bit DIBs and compares every pixel: cards, toggles, the candidate /
* notify preview, the sidebar, every page (card lists, sub-pages, 載入碼表),
* the breadcrumb, the hamburger, owner-drawn buttons, swatches (also through
* SettingsOwnerDraw's WM_DRAWITEM / WM_CTLCOLOR* handling), and the
* themed message box, in light and dark themes and at several dpis.
*
* Golden files (src/tests/golden/SettingsWindowPaint): the GDI / GDI+ calls
* of a separate Rust paint per case, recorded with synthetic text metrics
* (Golden::GdiRecording), so they do not depend on the machine's fonts;
* plus the Rust fonts' LOGFONTs and the WM_CTLCOLOR* results.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <gdiplus.h>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../BaseStructure.h"
#include "../DIMESettings/SettingsWindowPaint.h"
#include "../DIMESettings/SettingsOwnerDraw.h"
#include "../DIMESettings/SettingsPageLayout.h"
#include "dime_ffi.h"
#include "Golden.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SWP = SettingsWindowPaint;

namespace
{
    ULONG_PTR g_gdiplusToken = 0;

    // Paint into a fresh w x h DIB pre-filled with `bg`; return the pixels.
    std::vector<DWORD> Render(int w, int h, COLORREF bg, const std::function<void(HDC)>& paint)
    {
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bmp);
        HBRUSH br = CreateSolidBrush(bg);
        RECT rc = { 0, 0, w, h };
        FillRect(dc, &rc, br);
        DeleteObject(br);
        int saved = SaveDC(dc);
        paint(dc);
        RestoreDC(dc, saved);
        GdiFlush();
        std::vector<DWORD> px((DWORD*)bits, (DWORD*)bits + (size_t)w * h);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
        return px;
    }

    // The golden log of the running test method (null outside one).
    Golden::Log* g_golden = nullptr;

    // Records a separate Rust paint (synthetic metrics) as one golden line.
    void RecordRust(int w, int h, COLORREF bg, const std::function<void(HDC)>& rust, const std::wstring& what)
    {
        if (!g_golden) return;
        std::vector<std::wstring> calls;
        {
            Golden::GdiRecording rec;
            Render(w, h, bg, rust);
            calls = rec.Take();
        }
        Golden::Hash hash;
        std::wstring detail;
        for (const std::wstring& c : calls) {
            hash.Add(c);
            detail += c + L"\n";
        }
        g_golden->Add(what + L" " + std::to_wstring(w) + L"x" + std::to_wstring(h) + L": " + std::to_wstring(calls.size())
            + L" calls " + hash.Hex(), detail);
    }

    // Points g_golden at a test method's log for its lifetime.
    struct GoldenScope
    {
        explicit GoldenScope(Golden::Log& log) { g_golden = &log; }
        ~GoldenScope() { g_golden = nullptr; }
    };

    size_t InkCount(const std::vector<DWORD>& px)
    {
        size_t n = 0;
        for (DWORD p : px) if (p != px[0]) n++;
        return n;
    }

    void ExpectSame(int w, int h, COLORREF bg, const std::function<void(HDC)>& cpp,
        const std::function<void(HDC)>& rust, const std::wstring& what, bool needInk = true)
    {
        std::vector<DWORD> a = Render(w, h, bg, cpp);
        std::vector<DWORD> b = Render(w, h, bg, rust);
        if (needInk && InkCount(a) == 0)
            Assert::Fail((what + L": C++ painted nothing").c_str());
        for (size_t i = 0; i < a.size(); i++) {
            if ((a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF)) {
                wchar_t msg[256];
                swprintf_s(msg, L"%s: pixel (%d,%d) C++ %06X Rust %06X", what.c_str(),
                    (int)(i % w), (int)(i / w), a[i] & 0xFFFFFF, b[i] & 0xFFFFFF);
                Assert::Fail(msg);
            }
        }
        RecordRust(w, h, bg, rust, what);
    }

    SettingsSnapshot MakeSnapshot(int variant)
    {
        SettingsSnapshot s = {};
        static const wchar_t* faces[] = { L"Microsoft JhengHei", L"MingLiU", L"Segoe UI", L"NoSuchFontAtAll" };
        static const UINT sizes[] = { 12, 16, 20, 28 };
        wcscpy_s(s.fontFaceName, faces[variant % 4]);
        s.fontSize = sizes[(variant / 4) % 4];
        s.fontWeight = (variant & 1) ? FW_BOLD : FW_NORMAL;
        s.fontItalic = (variant / 2) % 2;
        bool dark = (variant % 3) == 1;
        s.itemColor = dark ? RGB(230, 230, 230) : RGB(0, 0, 0);
        s.phraseColor = RGB(0, 128, 0);
        s.numberColor = dark ? RGB(150, 150, 150) : RGB(0, 0, 255);
        s.itemBGColor = dark ? RGB(32, 32, 32) : RGB(250, 250, 250);
        s.selectedColor = RGB(255, 255, 255);
        s.selectedBGColor = RGB(0, 120, 215);
        s.colorMode = variant % 4;
        s.maxCodes = 4;
        return s;
    }

    struct Ctx
    {
        SWP::PaintContext ctx = {};
        SWP::Fonts fonts = {};
        HBRUSH sidebar = nullptr, content = nullptr;

        Ctx(bool dark, UINT dpi, int snapVariant, bool forceNoMdl2 = false)
        {
            SWP::CreateFonts(dpi, &fonts);
            ctx.theme = ComputeThemeColors(dark);
            ctx.snapshot = MakeSnapshot(snapVariant);
            sidebar = CreateSolidBrush(ctx.theme.sidebarBg);
            content = CreateSolidBrush(ctx.theme.contentBg);
            ctx.fontTitle = fonts.title;
            ctx.fontCardHeader = fonts.cardHeader;
            ctx.fontBody = fonts.body;
            ctx.fontDesc = fonts.desc;
            ctx.fontMDL2 = fonts.mdl2;
            ctx.fontMDL2Icon = fonts.mdl2Icon;
            ctx.brushSidebarBg = sidebar;
            ctx.brushContentBg = content;
            ctx.iconModule = GetModuleHandleW(nullptr);
            ctx.dpi = dpi;
            ctx.hasMDL2 = forceNoMdl2 ? 0 : fonts.hasMDL2;
            ctx.clientW = MulDiv(700, dpi, 96);
            ctx.clientH = MulDiv(900, dpi, 96);
            ctx.cardW = ctx.clientW - MulDiv(72, dpi, 96) - 17;
            ctx.sysFace = dark ? RGB(32, 32, 32) : RGB(240, 240, 240);
            ctx.hoverSidebarIndex = -1;
            ctx.hoverRowIndex = -1;
        }
        ~Ctx()
        {
            for (HGDIOBJ o : { (HGDIOBJ)fonts.title, (HGDIOBJ)fonts.cardHeader, (HGDIOBJ)fonts.body,
                     (HGDIOBJ)fonts.desc, (HGDIOBJ)fonts.mdl2, (HGDIOBJ)fonts.mdl2Icon, (HGDIOBJ)sidebar, (HGDIOBJ)content })
                if (o) DeleteObject(o);
        }
    };
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsWindowPaintParityTest)
    {
    public:
        TEST_CLASS_INITIALIZE(StartGdiplus)
        {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
        }

        TEST_CLASS_CLEANUP(StopGdiplus)
        {
            if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }

        TEST_METHOD(Fonts)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            for (UINT dpi : { 96u, 120u, 144u, 192u }) {
                SWP::Fonts a = {}, b = {};
                SWP::CreateFonts(dpi, &a);
                dime_settings_create_fonts(dpi, &b);
                Assert::AreEqual(a.hasMDL2, b.hasMDL2, L"hasMDL2");
                HFONT fa[] = { a.title, a.cardHeader, a.body, a.desc, a.mdl2, a.mdl2Icon };
                HFONT fb[] = { b.title, b.cardHeader, b.body, b.desc, b.mdl2, b.mdl2Icon };
                for (int i = 0; i < 6; i++) {
                    LOGFONTW la = {}, lb = {};
                    GetObjectW(fa[i], sizeof(la), &la);
                    GetObjectW(fb[i], sizeof(lb), &lb);
                    // CreateFontW copies 32 WCHARs from the face pointer, so the
                    // bytes after the NUL are whatever followed the string.
                    Assert::IsTrue(memcmp(&la, &lb, offsetof(LOGFONTW, lfFaceName)) == 0, L"LOGFONT fields");
                    Assert::AreEqual(std::wstring(la.lfFaceName), std::wstring(lb.lfFaceName), L"LOGFONT face");
                    golden.Add(L"dpi " + std::to_wstring(dpi) + L" font " + std::to_wstring(i) + L": height "
                        + std::to_wstring(lb.lfHeight) + L" weight " + std::to_wstring(lb.lfWeight) + L" italic "
                        + std::to_wstring(lb.lfItalic) + L" charset " + std::to_wstring(lb.lfCharSet) + L" quality "
                        + std::to_wstring(lb.lfQuality) + L" face [" + lb.lfFaceName + L"]");
                    DeleteObject(fa[i]);
                    DeleteObject(fb[i]);
                }
            }
            Golden::Check(L"SettingsWindowPaint", L"Fonts", golden);
        }

        TEST_METHOD(CardsTogglesSwatchesButtons)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            for (bool dark : { false, true }) {
                ThemeColors tc = ComputeThemeColors(dark);
                for (UINT dpi : { 96u, 144u, 192u }) {
                    int r = MulDiv(8, dpi, 96);
                    for (int hover = 0; hover < 2; hover++) {
                        ExpectSame(400, 120, tc.contentBg,
                            [&](HDC dc) { SWP::DrawCardWithShadow(dc, 10, 12, 360, 80, r, tc.cardBg, tc.cardBorder, tc.contentBg, hover != 0, tc.rowHover); },
                            [&](HDC dc) { dime_settings_draw_card(dc, 10, 12, 360, 80, r, tc.cardBg, tc.cardBorder, tc.contentBg, hover, tc.rowHover); },
                            L"card");
                    }
                    for (int on = 0; on < 2; on++) {
                        RECT rc = { 7, 5, 7 + MulDiv(40, dpi, 96), 5 + MulDiv(20, dpi, 96) };
                        ExpectSame(120, 60, tc.cardBg,
                            [&](HDC dc) { SWP::PaintToggleSwitch(dc, rc, on != 0, false, tc); },
                            [&](HDC dc) { dime_settings_paint_toggle(dc, &rc, on, 0, &tc); },
                            L"toggle");
                    }
                    RECT sw = { 3, 4, 3 + MulDiv(28, dpi, 96), 4 + MulDiv(28, dpi, 96) };
                    ExpectSame(80, 80, tc.cardBg,
                        [&](HDC dc) { SWP::PaintSwatch(dc, sw, RGB(200, 30, 90), tc.cardBorder, MulDiv(4, dpi, 96)); },
                        [&](HDC dc) { dime_settings_paint_swatch(dc, &sw, RGB(200, 30, 90), tc.cardBorder, MulDiv(4, dpi, 96)); },
                        L"swatch");

                    SWP::Fonts f = {};
                    SWP::CreateFonts(dpi, &f);
                    for (int state = 0; state < 3; state++) {
                        for (const wchar_t* text : { L"載入", L"還原預設值", L"" }) {
                            RECT rc = { 2, 3, 2 + MulDiv(120, dpi, 96), 3 + MulDiv(28, dpi, 96) };
                            ExpectSame(300, 80, RGB(255, 0, 255),
                                [&](HDC dc) { SWP::PaintOwnerButton(dc, rc, text, state == 1, state == 2, tc, f.body, dpi); },
                                [&](HDC dc) { dime_settings_paint_button(dc, &rc, (const uint16_t*)text, state == 1, state == 2, &tc, f.body, dpi); },
                                L"button");
                        }
                    }
                    for (HFONT o : { f.title, f.cardHeader, f.body, f.desc, f.mdl2, f.mdl2Icon }) DeleteObject(o);
                }
            }
            Golden::Check(L"SettingsWindowPaint", L"CardsTogglesSwatchesButtons", golden);
        }

        TEST_METHOD(CandidatePreview)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            for (int v = 0; v < 24; v++) {
                SettingsSnapshot s = MakeSnapshot(v);
                for (UINT dpi : { 96u, 144u }) {
                    for (COLORREF face : { RGB(240, 240, 240), RGB(32, 32, 32) }) {
                        HDC screen = GetDC(nullptr);
                        int a = SWP::CandidatePreviewHeight(screen, s, dpi);
                        int b = dime_settings_preview_height(screen, &s, dpi);
                        ReleaseDC(nullptr, screen);
                        Assert::AreEqual(a, b, L"preview height");
                        ExpectSame(600, a + 40, face,
                            [&](HDC dc) { SWP::PaintCandidatePreview(dc, s, 20, 20, 500, a, dpi, face); },
                            [&](HDC dc) { dime_settings_paint_preview(dc, &s, 20, 20, 500, a, dpi, face); },
                            L"preview " + std::to_wstring(v));
                    }
                }
            }
            Golden::Check(L"SettingsWindowPaint", L"CandidatePreview", golden);
        }

        TEST_METHOD(SidebarAndBreadcrumb)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            for (bool dark : { false, true }) {
                for (UINT dpi : { 96u, 144u }) {
                    for (int sel = 0; sel < 6; sel++) {
                        Ctx c(dark, dpi, sel);
                        c.ctx.selectedModeIndex = sel;
                        c.ctx.hoverSidebarIndex = (sel + 2) % 6;
                        c.ctx.currentCardIndex = sel % 3;
                        c.ctx.sidebarNarrowMode = sel % 2;
                        ExpectSame(MulDiv(300, dpi, 96), c.ctx.clientH, RGB(255, 0, 255),
                            [&](HDC dc) { SWP::PaintSidebar(dc, c.ctx); },
                            [&](HDC dc) { dime_settings_paint_sidebar(dc, &c.ctx); },
                            L"sidebar");
                        ExpectSame(c.ctx.clientW, MulDiv(44, dpi, 96), c.ctx.theme.contentBg,
                            [&](HDC dc) { SWP::PaintBreadcrumb(dc, c.ctx); },
                            [&](HDC dc) { dime_settings_paint_breadcrumb(dc, &c.ctx); },
                            L"breadcrumb");
                        for (int noMdl2 = 0; noMdl2 < 2; noMdl2++) {
                            Ctx h(dark, dpi, sel, noMdl2 != 0);
                            RECT rc = { 4, 2, 4 + MulDiv(36, dpi, 96), 2 + MulDiv(36, dpi, 96) };
                            ExpectSame(80, 80, RGB(255, 0, 255),
                                [&](HDC dc) { SWP::PaintHamburger(dc, h.ctx, rc, h.content); },
                                [&](HDC dc) { dime_settings_paint_hamburger(dc, &h.ctx, &rc, h.content); },
                                L"hamburger");
                        }
                    }
                    Ctx collapsed(dark, dpi, 0);
                    collapsed.ctx.sidebarCollapsed = 1;
                    ExpectSame(200, 200, RGB(255, 0, 255),
                        [&](HDC dc) { SWP::PaintSidebar(dc, collapsed.ctx); },
                        [&](HDC dc) { dime_settings_paint_sidebar(dc, &collapsed.ctx); },
                        L"collapsed sidebar", false);
                }
            }
            Golden::Check(L"SettingsWindowPaint", L"SidebarAndBreadcrumb", golden);
        }

        TEST_METHOD(Pages)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            int imeCount = 0;
            GetIMEPageLayout(&imeCount);
            std::mt19937 rng(31);
            int cases = 0;
            for (bool dark : { false, true }) {
                for (UINT dpi : { 96u, 144u }) {
                    for (int mode = 0; mode < 4; mode++) {
                        for (int nav = -1; nav < imeCount; nav++) {
                            Ctx c(dark, dpi, mode + 4 * (int)(rng() % 6));
                            c.ctx.selectedModeIndex = mode;
                            c.ctx.currentMode = mode;
                            c.ctx.navLevel = nav < 0 ? 0 : 1;
                            c.ctx.currentCardIndex = nav < 0 ? 0 : nav;
                            c.ctx.scrollPos = (int)(rng() % 3) * 40;
                            c.ctx.sidebarNarrowMode = (int)(rng() % 2);
                            c.ctx.colorGridExpanded = (int)(rng() % 2);
                            c.ctx.snapshot.colorMode = (rng() % 2) ? 3 : 1;
                            c.ctx.hoverRowIndex = (int)(rng() % 4) - 1;
                            for (int i = 0; i < SettingsWindowLayout::kExpandedSlots; i++)
                                c.ctx.cardExpanded[i] = (rng() % 2) != 0;
                            for (int i = 0; i < SWP::kToggleSlots; i++)
                                c.ctx.toggleOn[i] = (BYTE)(rng() % 2);
                            ExpectSame(c.ctx.clientW, c.ctx.clientH, RGB(255, 0, 255),
                                [&](HDC dc) { SWP::PaintContent(dc, c.ctx); },
                                [&](HDC dc) { dime_settings_paint_content(dc, &c.ctx); },
                                L"page mode=" + std::to_wstring(mode) + L" card=" + std::to_wstring(nav)
                                    + L" dpi=" + std::to_wstring(dpi) + (dark ? L" dark" : L" light"));
                            cases++;
                        }
                    }
                    for (int exp = 0; exp < 2; exp++) {
                        Ctx c(dark, dpi, 0, exp == 1);
                        c.ctx.selectedModeIndex = 5;
                        for (int i = 0; i < SettingsWindowLayout::kExpandedSlots; i++)
                            c.ctx.cardExpanded[i] = exp != 0;
                        ExpectSame(c.ctx.clientW, c.ctx.clientH, RGB(255, 0, 255),
                            [&](HDC dc) { SWP::PaintContent(dc, c.ctx); },
                            [&](HDC dc) { dime_settings_paint_content(dc, &c.ctx); },
                            L"load-table page");
                    }
                }
            }
            Assert::IsTrue(cases >= 32, L"page cases");
            Golden::Check(L"SettingsWindowPaint", L"Pages", golden);
        }

        TEST_METHOD(WindowFrames)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            int frames = 0;
            for (bool dark : { false, true }) {
                for (UINT dpi : { 96u, 144u }) {
                    for (int narrow = 0; narrow < 2; narrow++) {
                        for (int collapsed = 0; collapsed < 2; collapsed++) {
                            for (int nav = 0; nav < 2; nav++) {
                                for (int sel : { 0, 3, 5 }) {
                                    Ctx m(dark, dpi, sel);
                                    m.ctx.selectedModeIndex = sel;
                                    m.ctx.currentMode = sel < 4 ? sel : 0;
                                    m.ctx.navLevel = nav;
                                    m.ctx.currentCardIndex = 0;
                                    m.ctx.sidebarNarrowMode = narrow;
                                    m.ctx.sidebarCollapsed = collapsed;
                                    m.ctx.hoverSidebarIndex = 2;
                                    SWP::PaintContext content = m.ctx;
                                    content.clientW = m.ctx.clientW - (narrow ? 0 : MulDiv(280, dpi, 96));
                                    content.clientH = m.ctx.clientH - (nav == 1 && sel < 4 ? MulDiv(44, dpi, 96) : 0);
                                    for (int hasContent = 0; hasContent < 2; hasContent++) {
                                        ExpectSame(m.ctx.clientW, m.ctx.clientH, RGB(255, 0, 255),
                                            [&](HDC dc) { SWP::PaintMainFrame(dc, m.ctx, hasContent != 0); },
                                            [&](HDC dc) { dime_settings_paint_main_frame(dc, &m.ctx, hasContent); },
                                            L"main frame", collapsed == 0 || (nav == 1 && sel < 4 && hasContent != 0));
                                        frames++;
                                    }
                                    ExpectSame(content.clientW, content.clientH, RGB(255, 0, 255),
                                        [&](HDC dc) { SWP::PaintContentFrame(dc, content, m.ctx); },
                                        [&](HDC dc) { dime_settings_paint_content_frame(dc, &content, &m.ctx); },
                                        L"content frame narrow=" + std::to_wstring(narrow) + L" collapsed=" + std::to_wstring(collapsed)
                                            + L" nav=" + std::to_wstring(nav) + L" sel=" + std::to_wstring(sel));
                                    frames++;
                                }
                            }
                        }
                    }
                }
            }
            Assert::IsTrue(frames >= 144, L"frame cases");
            Golden::Check(L"SettingsWindowPaint", L"WindowFrames", golden);
        }

        // SettingsOwnerDraw (5b-8j): WM_DRAWITEM for buttons, swatches, and
        // items that are not ours, through the dispatch-free Cpp / Rust pair.
        TEST_METHOD(OwnerDrawItems)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            HWND btn = CreateWindowExW(0, L"BUTTON", L"", WS_POPUP | BS_OWNERDRAW, 0, 0, 10, 10,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            Assert::IsNotNull(btn, L"button");
            std::wstring longText(140, L'\x5B57');
            const wchar_t* texts[] = { L"\x5132\x5B58", L"\x8B8A\x66F4\x5B57\x578B", L"OK", L"", longText.c_str() };
            int painted = 0;
            for (bool dark : { false, true }) {
                for (UINT dpi : { 96u, 144u, 0u }) {
                    SWP::Fonts f = {};
                    SWP::CreateFonts(dpi ? dpi : 96, &f);
                    SettingsOwnerDraw::Context ctx = {};
                    ctx.theme = ComputeThemeColors(dark);
                    ctx.fontBody = f.body;
                    ctx.dpi = dpi;
                    COLORREF sw[6] = { RGB(0, 0, 0), RGB(255, 255, 255), RGB(250, 250, 250), RGB(0, 128, 0),
                        RGB(0, 0, 255), RGB(0, 120, 215) };
                    memcpy(ctx.swatch, sw, sizeof(sw));
                    int w = MulDiv(160, dpi ? dpi : 96, 96), h = MulDiv(40, dpi ? dpi : 96, 96);
                    struct Item { UINT type; UINT id; };
                    std::vector<Item> items;
                    for (UINT id = 3998; id <= 4007; id++) items.push_back({ ODT_STATIC, id });
                    items.push_back({ ODT_BUTTON, 201 });
                    items.push_back({ ODT_COMBOBOX, 4000 });
                    items.push_back({ ODT_LISTBOX, 4001 });
                    for (const Item& it : items) {
                        for (const wchar_t* text : texts) {
                            if (it.type != ODT_BUTTON && text != texts[0]) continue;
                            SetWindowTextW(btn, text);
                            for (UINT state : { 0u, (UINT)ODS_SELECTED, (UINT)ODS_DISABLED, (UINT)(ODS_SELECTED | ODS_DISABLED), (UINT)ODS_FOCUS }) {
                                if (it.type != ODT_BUTTON && state) continue;
                                DRAWITEMSTRUCT dis = {};
                                dis.CtlType = it.type;
                                dis.CtlID = it.id;
                                dis.itemState = state;
                                dis.hwndItem = btn;
                                dis.rcItem = { 2, 3, w - 2, h - 3 };
                                bool ra = false, rb = false;
                                bool ours = (it.type == ODT_BUTTON) || (it.type == ODT_STATIC && it.id >= 4000 && it.id < 4006);
                                ExpectSame(w, h, RGB(255, 0, 255),
                                    [&](HDC dc) { DRAWITEMSTRUCT d = dis; d.hDC = dc; ra = SettingsOwnerDraw::Rust::DrawItem(&d, ctx); },
                                    [&](HDC dc) { DRAWITEMSTRUCT d = dis; d.hDC = dc; rb = SettingsOwnerDraw::Rust::DrawItem(&d, ctx); },
                                    L"draw item type=" + std::to_wstring(it.type) + L" id=" + std::to_wstring(it.id)
                                        + L" state=" + std::to_wstring(state) + L" dark=" + std::to_wstring(dark)
                                        + L" dpi=" + std::to_wstring(dpi),
                                    ours);
                                Assert::AreEqual(ra, rb, L"handled");
                                Assert::AreEqual(ours, ra, L"handled as expected");
                                if (ra) painted++;
                            }
                        }
                    }
                    for (HGDIOBJ o : { (HGDIOBJ)f.title, (HGDIOBJ)f.cardHeader, (HGDIOBJ)f.body, (HGDIOBJ)f.desc,
                             (HGDIOBJ)f.mdl2, (HGDIOBJ)f.mdl2Icon })
                        if (o) DeleteObject(o);
                }
            }
            DestroyWindow(btn);
            Assert::IsTrue(painted >= 100, L"painted items");
            Golden::Check(L"SettingsWindowPaint", L"OwnerDrawItems", golden);
        }

        // SettingsOwnerDraw (5b-8j): WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX.
        TEST_METHOD(OwnerCtlColor)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            HDC screen = GetDC(nullptr);
            for (bool dark : { false, true }) {
                ThemeColors tc = ComputeThemeColors(dark);
                for (int preset = 0; preset < 2; preset++) {
                    HDC da = CreateCompatibleDC(screen), db = CreateCompatibleDC(screen);
                    SetTextColor(da, RGB(1, 2, 3)); SetTextColor(db, RGB(1, 2, 3));
                    SetBkColor(da, RGB(4, 5, 6)); SetBkColor(db, RGB(4, 5, 6));
                    HBRUSH given = preset ? CreateSolidBrush(RGB(9, 9, 9)) : nullptr;
                    HBRUSH ba = given, bb = given;
                    bool ra = SettingsOwnerDraw::Rust::CtlColor(da, dark, tc, &ba);
                    bool rb = SettingsOwnerDraw::Rust::CtlColor(db, dark, tc, &bb);
                    std::wstring what = L"dark=" + std::to_wstring(dark) + L" preset=" + std::to_wstring(preset);
                    Assert::AreEqual(ra, rb, (what + L" handled").c_str());
                    Assert::AreEqual(dark, ra, (what + L" handled only in dark").c_str());
                    Assert::AreEqual((DWORD)GetTextColor(da), (DWORD)GetTextColor(db), (what + L" text").c_str());
                    Assert::AreEqual((DWORD)GetBkColor(da), (DWORD)GetBkColor(db), (what + L" bk").c_str());
                    Assert::AreEqual(ba == given, bb == given, (what + L" brush kept").c_str());
                    Assert::AreEqual(ba != nullptr, bb != nullptr, (what + L" brush exists").c_str());
                    LOGBRUSH lg = {};
                    if (bb) GetObjectW(bb, sizeof(lg), &lg);
                    wchar_t line[160];
                    swprintf_s(line, L"%s: handled %d text %06X bk %06X brush %s %06X", what.c_str(), (int)rb,
                        (unsigned)GetTextColor(db), (unsigned)GetBkColor(db), bb == given ? L"given" : (bb ? L"new" : L"none"),
                        bb ? (unsigned)lg.lbColor : 0u);
                    golden.Add(line);
                    if (ba && ba != given) {
                        LOGBRUSH la = {}, lb = {};
                        GetObjectW(ba, sizeof(la), &la);
                        GetObjectW(bb, sizeof(lb), &lb);
                        Assert::IsTrue(la.lbStyle == lb.lbStyle && la.lbColor == lb.lbColor, (what + L" brush").c_str());
                        DeleteObject(ba);
                        DeleteObject(bb);
                    }
                    if (given) DeleteObject(given);
                    DeleteDC(da);
                    DeleteDC(db);
                }
            }
            ReleaseDC(nullptr, screen);
            Golden::Check(L"SettingsWindowPaint", L"OwnerCtlColor", golden);
        }

        TEST_METHOD(ThemedMessageBoxPaint)
        {
            Golden::Log golden;
            GoldenScope scope(golden);
            static const wchar_t* labels[] = { L"是(Y)", L"否(N)", L"取消" };
            for (bool dark : { false, true }) {
                ThemeColors tc = ComputeThemeColors(dark);
                for (UINT dpi : { 96u, 144u }) {
                    HFONT font = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                    for (int count = 1; count <= 3; count++) {
                        for (int icon = 0; icon < 2; icon++) {
                            HICON hIcon = icon ? LoadIcon(nullptr, IDI_WARNING) : nullptr;
                            SettingsWindowLayout::MsgBoxLayout L =
                                SettingsWindowLayout::MsgBoxLayoutFor(hIcon != nullptr, count, 250, 40, dpi);
                            const wchar_t* text = L"儲存失敗：偵測到 3 個格式錯誤行，第一個錯誤在第 2 行。";
                            ExpectSame(L.rcClient.right, L.rcClient.bottom, RGB(255, 0, 255),
                                [&](HDC dc) { SWP::PaintMsgBox(dc, L, text, labels, count, count - 1, 0, tc, font, hIcon, dpi); },
                                [&](HDC dc) { dime_settings_paint_msgbox(dc, &L, (const uint16_t*)text, (const uint16_t* const*)labels, count, count - 1, 0, &tc, font, hIcon, dpi); },
                                L"msgbox");
                        }
                    }
                    DeleteObject(font);
                }
            }
            Golden::Check(L"SettingsWindowPaint", L"ThemedMessageBoxPaint", golden);
        }
    };
}

// SettingsWindowPaint.cpp — painting of the settings window. See
// SettingsWindowPaint.h. The Cpp:: functions are the code that used to live
// in SettingsWindow.cpp; dime-settings (paint.rs) mirrors them call by call.

#include <windows.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <math.h>
#include "..\BaseStructure.h"
#include "SettingsWindowPaint.h"
#include "SettingsPageLayout.h"
#include "dime_ffi.h"
#include <cstddef>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

namespace SettingsWindowPaint {

// ============================================================================
// Dispatch
// ============================================================================

#ifdef _WIN64
static_assert(sizeof(PaintContext) == 520, "PaintContext must match dime-settings");
static_assert(offsetof(PaintContext, dpi) == 376, "PaintContext::dpi");
static_assert(sizeof(Fonts) == 56, "Fonts must match dime-settings");
#else
static_assert(sizeof(PaintContext) == 484, "PaintContext must match dime-settings");
static_assert(offsetof(PaintContext, dpi) == 340, "PaintContext::dpi");
static_assert(sizeof(Fonts) == 28, "Fonts must match dime-settings");
#endif
static_assert(offsetof(PaintContext, snapshot) == 88, "PaintContext::snapshot");
static_assert(offsetof(PaintContext, fontTitle) == 304, "PaintContext::fontTitle");
static_assert(offsetof(PaintContext, toggleOn) == offsetof(PaintContext, cardExpanded) + 16, "PaintContext::toggleOn");

void CreateFonts(UINT dpi, Fonts* out) { dime_settings_create_fonts(dpi, out); }

void DrawCardWithShadow(HDC hdc, int x, int y, int w, int h, int cornerR,
    COLORREF cardBg, COLORREF cardBorder, COLORREF contentBg, bool isHover, COLORREF hoverColor)
{
    dime_settings_draw_card(hdc, x, y, w, h, cornerR, cardBg, cardBorder, contentBg, isHover ? 1 : 0, hoverColor);
}

void PaintToggleSwitch(HDC hdc, RECT rc, bool isOn, bool isHover, const ThemeColors& tc)
{
    dime_settings_paint_toggle(hdc, &rc, isOn ? 1 : 0, isHover ? 1 : 0, &tc);
}

int CandidatePreviewHeight(HDC hdc, const SettingsSnapshot& s, UINT dpi)
{
    return dime_settings_preview_height(hdc, &s, dpi);
}

void PaintCandidatePreview(HDC hdc, const SettingsSnapshot& s, int margin, int curY,
    int cardW, int totalH, UINT dpi, COLORREF sysFace)
{
    dime_settings_paint_preview(hdc, &s, margin, curY, cardW, totalH, dpi, sysFace);
}

void PaintSidebar(HDC hdc, const PaintContext& ctx) { dime_settings_paint_sidebar(hdc, &ctx); }
void PaintContent(HDC hdc, const PaintContext& ctx) { dime_settings_paint_content(hdc, &ctx); }
void PaintBreadcrumb(HDC hdc, const PaintContext& ctx) { dime_settings_paint_breadcrumb(hdc, &ctx); }

void PaintHamburger(HDC hdc, const PaintContext& ctx, RECT rc, HBRUSH bg)
{
    dime_settings_paint_hamburger(hdc, &ctx, &rc, bg);
}

void PaintOwnerButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed, bool disabled,
    const ThemeColors& tc, HFONT font, UINT dpi)
{
    dime_settings_paint_button(hdc, &rc, reinterpret_cast<const uint16_t*>(text),
        pressed ? 1 : 0, disabled ? 1 : 0, &tc, font, dpi);
}

void PaintSwatch(HDC hdc, RECT rc, COLORREF color, COLORREF border, int radius)
{
    dime_settings_paint_swatch(hdc, &rc, color, border, radius);
}

void PaintMainFrame(HDC hdc, const PaintContext& main, bool hasContent)
{
    dime_settings_paint_main_frame(hdc, &main, hasContent ? 1 : 0);
}

void PaintContentFrame(HDC hdc, const PaintContext& content, const PaintContext& main)
{
    dime_settings_paint_content_frame(hdc, &content, &main);
}

void PaintMsgBox(HDC hdc, const SettingsWindowLayout::MsgBoxLayout& L, const wchar_t* text,
    const wchar_t* const* labels, int btnCount, int hoverBtn, int defaultBtn,
    const ThemeColors& tc, HFONT font, HICON icon, UINT dpi)
{
    dime_settings_paint_msgbox(hdc, &L, reinterpret_cast<const uint16_t*>(text),
        reinterpret_cast<const uint16_t* const*>(labels), btnCount, hoverBtn, defaultBtn, &tc, font, icon, dpi);
}

} // namespace SettingsWindowPaint

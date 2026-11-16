// SettingsGeometry.cpp — Geometry constants + theme color derivation

#include "SettingsGeometry.h"
#include "..\Define.h"
#include <cstddef>
#include "dime_ffi.h"

// SettingsGeometry / ThemeColors must match dime-config::geometry.
static_assert(sizeof(SettingsGeometry) == 260, "SettingsGeometry size");
static_assert(alignof(SettingsGeometry) == 4, "SettingsGeometry align");
static_assert(offsetof(SettingsGeometry, sidebarWidth) == 0, "sidebarWidth");
static_assert(offsetof(SettingsGeometry, windowDefaultH) == 256, "windowDefaultH");
static_assert(sizeof(ThemeColors) == 88, "ThemeColors size");
static_assert(alignof(ThemeColors) == 4, "ThemeColors align");
static_assert(offsetof(ThemeColors, accent) == 40, "accent");
static_assert(offsetof(ThemeColors, buttonTextDisabled) == 84, "buttonTextDisabled");
static_assert(sizeof(DimeSysColors) == 24, "DimeSysColors size");

#pragma comment(lib, "dwmapi.lib")

namespace {
SettingsGeometry LoadRustGeometry()
{
    SettingsGeometry geo = {};
    dime_settings_geometry(&geo);
    return geo;
}
}
const SettingsGeometry g_geo = LoadRustGeometry();

// ============================================================================
// Blend two colors
// ============================================================================
COLORREF BlendColor(COLORREF c1, COLORREF c2, int alpha)
{
    return dime_settings_blend_color(c1, c2, alpha);
}

// ============================================================================
// Compute theme colors from system APIs
// ============================================================================
ThemeColors ComputeThemeColors(bool isDark)
{
    DimeSysColors sys = {};
    sys.hotlight = GetSysColor(COLOR_HOTLIGHT);
    sys.face = GetSysColor(COLOR_3DFACE);
    sys.window = GetSysColor(COLOR_WINDOW);
    sys.window_text = GetSysColor(COLOR_WINDOWTEXT);
    sys.gray_text = GetSysColor(COLOR_GRAYTEXT);
    sys.highlight_text = GetSysColor(COLOR_HIGHLIGHTTEXT);
    ThemeColors tc = {};
    dime_settings_theme_colors(isDark ? 1 : 0, &sys, &tc);
    return tc;
}

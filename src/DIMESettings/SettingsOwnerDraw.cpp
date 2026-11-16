// SettingsOwnerDraw.cpp — see SettingsOwnerDraw.h. The Cpp functions are the
// code that used to be inline in the content area's WM_DRAWITEM and
// WM_CTLCOLOR*; dime-settings (owner.rs) mirrors them.

#include <windows.h>
#include <cstddef>
#include "..\BaseStructure.h"
#include "SettingsOwnerDraw.h"
#include "SettingsWindowPaint.h"
#include "dime_ffi.h"

namespace SettingsOwnerDraw {

namespace Rust {

bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx)
{
    return dime_settings_draw_item(dis, &ctx) != 0;
}

bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush)
{
    return dime_settings_ctl_color(hdc, isDark ? 1 : 0, &theme, reinterpret_cast<void**>(brush)) != 0;
}

} // namespace Rust

namespace Impl = Rust;

bool DrawItem(const DRAWITEMSTRUCT* dis, const Context& ctx) { return Impl::DrawItem(dis, ctx); }
bool CtlColor(HDC hdc, bool isDark, const ThemeColors& theme, HBRUSH* brush)
{
    return Impl::CtlColor(hdc, isDark, theme, brush);
}

} // namespace SettingsOwnerDraw

// SettingsLifecycle.cpp — see SettingsLifecycle.h. The Cpp functions follow
// the code that used to be inline in wWinMain, WM_COPYDATA, SwitchMode, and
// RepositionControlsForScroll; dime-settings (lifecycle.rs) mirrors them.

#include <windows.h>
#include "SettingsLifecycle.h"
#include "SettingsController.h"
#include "SettingsGeometry.h"
#include "dime_ffi.h"

namespace SettingsLifecycle {

static_assert(sizeof(LaunchArgs) == sizeof(DimeLaunchArgs), "LaunchArgs");
static_assert(sizeof(ScrollItem) == sizeof(DimeScrollItem) && sizeof(ScrollItem) == 20, "ScrollItem");
static_assert(sizeof(ScrollAction) == sizeof(DimeScrollAction) && sizeof(ScrollAction) == 16, "ScrollAction");
static_assert(sizeof(bool) == 1, "bool");

namespace Rust {

LaunchArgs ParseLaunch(const wchar_t* cmdLine)
{
    DimeLaunchArgs r = {};
    r.gui_mode = (int)IME_MODE::IME_MODE_DAYI;
    dime_settings_parse_launch(reinterpret_cast<const uint16_t*>(cmdLine), &r);
    return { r.legacy, r.reset_ui, r.new_ui, r.gui_mode };
}

IME_MODE CopyDataMode(const wchar_t* cmdLine)
{
    return (IME_MODE)dime_settings_copy_data_mode(reinterpret_cast<const uint16_t*>(cmdLine));
}

void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots])
{
    uint8_t raw[SettingsWindowLayout::kExpandedSlots] = {};
    dime_settings_default_expanded(ime, imeCount, load, loadCount, raw);
    for (int i = 0; i < SettingsWindowLayout::kExpandedSlots; i++)
        out[i] = raw[i] != 0;
}

std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi)
{
    std::vector<ScrollAction> out(items.size());
    if (!items.empty())
        dime_settings_plan_scroll(reinterpret_cast<const DimeScrollItem*>(items.data()), items.size(), scrollPos, dpi,
            reinterpret_cast<DimeScrollAction*>(out.data()));
    return out;
}

} // namespace Rust

namespace Impl = Rust;

LaunchArgs ParseLaunch(const wchar_t* cmdLine) { return Impl::ParseLaunch(cmdLine); }
IME_MODE CopyDataMode(const wchar_t* cmdLine) { return Impl::CopyDataMode(cmdLine); }
void DefaultExpanded(const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount,
    bool out[SettingsWindowLayout::kExpandedSlots])
{
    Impl::DefaultExpanded(ime, imeCount, load, loadCount, out);
}
std::vector<ScrollAction> PlanScroll(const std::vector<ScrollItem>& items, int scrollPos, UINT dpi)
{
    return Impl::PlanScroll(items, scrollPos, dpi);
}

} // namespace SettingsLifecycle

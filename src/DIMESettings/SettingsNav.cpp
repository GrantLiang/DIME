// SettingsNav.cpp — see SettingsNav.h. Cpp::Handle is the logic that used to
// live in SettingsWindow::WndProc / ContentWndProc (WM_SIZE, WM_MOUSEWHEEL,
// WM_VSCROLL, WM_MOUSEMOVE, WM_MOUSELEAVE, WM_LBUTTONDOWN), with every side
// effect turned into a NavEffect. dime-settings (nav.rs) mirrors it.

#include <windows.h>
#include "..\BaseStructure.h"
#include "SettingsNav.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsGeometry.h"
#include "dime_ffi.h"
#include <cstddef>

namespace SettingsNav {

static_assert(sizeof(NavState) == 64, "NavState must match dime-settings");
static_assert(sizeof(NavEvent) == 36, "NavEvent must match dime-settings");
static_assert(sizeof(NavEffect) == 32, "NavEffect must match dime-settings");

static_assert(offsetof(NavState, cardExpanded) == 48, "NavState::cardExpanded");

void Handle(NavState& s, const NavEvent& ev, std::vector<NavEffect>& out)
{
    NavEffect buf[32];
    size_t count = 0;
    dime_settings_nav_handle(&s, &ev, buf, 32, &count);
    for (size_t i = 0; i < count && i < 32; i++) out.push_back(buf[i]);
}

POINT MinTrackSize(UINT screenDpi)
{
    POINT p = {};
    dime_settings_nav_min_track_size(screenDpi, &p);
    return p;
}

} // namespace SettingsNav

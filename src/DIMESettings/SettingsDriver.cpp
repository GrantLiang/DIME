// SettingsDriver.cpp — see SettingsDriver.h. The Cpp functions are what used
// to be SettingsWindow::RunNav, RunFrame, SwitchMode, RebuildContentArea,
// UpdateScrollInfo, RepositionControlsForScroll, and ShowAllControls;
// dime-settings (driver.rs) mirrors them. Calls between Cpp functions are
// written Cpp:: (ADL on Host would also find the dispatched ones).

#include <windows.h>
#include "..\BaseStructure.h"
#include "SettingsDriver.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "SettingsLifecycle.h"
#include "dime_ffi.h"

namespace SettingsDriver {

static_assert(sizeof(State) == 72, "State must match dime-settings DriverState");
static_assert(sizeof(ControlInfo) == 32, "ControlInfo must match dime-settings");

namespace Rust {

namespace {
// C thunks over Host. Host methods do not throw; catch anyway so nothing
// unwinds into Rust.
Host& H(void* ctx) { return *static_cast<Host*>(ctx); }

uint32_t MainDpi(void* c) { try { return H(c).MainDpi(); } catch (...) { return 96; } }
uint32_t ContentDpi(void* c) { try { return H(c).ContentDpi(); } catch (...) { return 96; } }
int32_t HasContent(void* c) { try { return H(c).HasContent() ? 1 : 0; } catch (...) { return 0; } }
void MainClient(void* c, int32_t* w, int32_t* ht)
{
    int a = 0, b = 0;
    try { H(c).MainClient(a, b); } catch (...) {}
    *w = a;
    *ht = b;
}
int32_t ContentClientHeight(void* c) { try { return H(c).ContentClientHeight(); } catch (...) { return 0; } }
int32_t VScrollWidth(void* c) { try { return H(c).VScrollWidth(); } catch (...) { return 0; } }
int32_t SystemDark(void* c) { try { return H(c).SystemDark() ? 1 : 0; } catch (...) { return 0; } }
int32_t ColorMode(void* c) { try { return H(c).ColorMode(); } catch (...) { return 0; } }
int32_t SaveEnabled(void* c) { try { return H(c).SaveEnabled() ? 1 : 0; } catch (...) { return 0; } }
int32_t PreviewHeight(void* c, uint32_t dpi) { try { return H(c).PreviewHeight(dpi); } catch (...) { return 0; } }
int32_t ControlCount(void* c) { try { return H(c).ControlCount(); } catch (...) { return 0; } }
void Control(void* c, int32_t i, void* out)
{
    ControlInfo info = {};
    try { info = H(c).Control(i); } catch (...) {}
    *static_cast<ControlInfo*>(out) = info;
}
void MoveControl(void* c, int32_t i, int32_t x, int32_t y, int32_t w, int32_t ht)
{
    try { H(c).MoveControl(i, x, y, w, ht); } catch (...) {}
}
void ShowControl(void* c, int32_t i, int32_t show) { try { H(c).ShowControl(i, show != 0); } catch (...) {} }
void ResetControlRegion(void* c, int32_t i) { try { H(c).ResetControlRegion(i); } catch (...) {} }
int32_t ControlChecked(void* c, int32_t i) { try { return H(c).ControlChecked(i) ? 1 : 0; } catch (...) { return 0; } }
void SetControlChecked(void* c, int32_t i, int32_t on) { try { H(c).SetControlChecked(i, on != 0); } catch (...) {} }
void DestroyControls(void* c) { try { H(c).DestroyControls(); } catch (...) {} }
void CreateControls(void* c, const void* in, int32_t cardW)
{
    try { H(c).CreateControls(*static_cast<const SettingsWindowLayout::PlanInput*>(in), cardW); } catch (...) {}
}
void Populate(void* c) { try { H(c).Populate(); } catch (...) {} }
void InvalidateMain(void* c, const int32_t* rc, int32_t erase)
{
    try { H(c).InvalidateMain((const RECT*)rc, erase != 0); } catch (...) {}
}
void InvalidateContent(void* c, int32_t erase) { try { H(c).InvalidateContent(erase != 0); } catch (...) {} }
void TrackLeave(void* c, int32_t content) { try { H(c).TrackLeave(content != 0); } catch (...) {} }
void SetScroll(void* c, int32_t total, int32_t page, int32_t pos) { try { H(c).SetScrollInfo(total, page, pos); } catch (...) {} }
void RedrawContent(void* c) { try { H(c).RedrawContent(); } catch (...) {} }
void UpdateContent(void* c) { try { H(c).UpdateContent(); } catch (...) {} }
void MoveContent(void* c, int32_t x, int32_t y, int32_t w, int32_t ht) { try { H(c).MoveContent(x, y, w, ht); } catch (...) {} }
void FocusContent(void* c) { try { H(c).FocusContent(); } catch (...) {} }
void CreateContent(void* c, int32_t x, int32_t y, int32_t w, int32_t ht) { try { H(c).CreateContent(x, y, w, ht); } catch (...) {} }
void ContentTheme(void* c, int32_t dark) { try { H(c).ContentTheme(dark != 0); } catch (...) {} }
void SetMainPos(void* c, int32_t x, int32_t y, int32_t w, int32_t ht) { try { H(c).SetMainPos(x, y, w, ht); } catch (...) {} }
void SetDarkTitle(void* c, int32_t dark) { try { H(c).SetDarkTitle(dark != 0); } catch (...) {} }
void SetCaptionColor(void* c, int32_t attr) { try { H(c).SetCaptionColor(attr); } catch (...) {} }
void ThemeBrushes(void* c) { try { H(c).ThemeBrushes(); } catch (...) {} }
void CreateFonts(void* c, int32_t dpi) { try { H(c).CreateFonts(dpi); } catch (...) {} }
void DestroyFonts(void* c) { try { H(c).DestroyFonts(); } catch (...) {} }
int32_t AskSaveBeforeBack(void* c) { try { return H(c).AskSaveBeforeBack(); } catch (...) { return IDCANCEL; } }
void ClickSave(void* c) { try { H(c).ClickSave(); } catch (...) {} }
void ToggleSnapshot(void* c, int32_t id, int32_t on) { try { H(c).ToggleSnapshot(id, on != 0); } catch (...) {} }
void ApplyAndSave(void* c) { try { H(c).ApplyAndSave(); } catch (...) {} }
void EnumerateReverseProviders(void* c) { try { H(c).EnumerateReverseProviders(); } catch (...) {} }
void LoadConfig(void* c, int32_t mode) { try { H(c).LoadConfig(mode); } catch (...) {} }
void LoadColors(void* c) { try { H(c).LoadColors(); } catch (...) {} }
void LoadSnapshot(void* c) { try { H(c).LoadSnapshot(); } catch (...) {} }
void RecreateEngine(void* c, int32_t mode) { try { H(c).RecreateEngine(mode); } catch (...) {} }

// Order must match DimeSettingsDriverHostVtbl (dime_ffi.h) and
// DriverHostVtbl (settings_window.rs).
const DimeSettingsDriverHostVtbl kVtbl = {
    MainDpi, ContentDpi, HasContent, MainClient, ContentClientHeight, VScrollWidth, SystemDark,
    ColorMode, SaveEnabled, PreviewHeight,
    ControlCount, Control, MoveControl, ShowControl, ResetControlRegion,
    ControlChecked, SetControlChecked, DestroyControls, CreateControls, Populate,
    InvalidateMain, InvalidateContent, TrackLeave, SetScroll, RedrawContent, UpdateContent,
    MoveContent, FocusContent, CreateContent, ContentTheme, SetMainPos, SetDarkTitle,
    SetCaptionColor, ThemeBrushes, CreateFonts, DestroyFonts,
    AskSaveBeforeBack, ClickSave,
    ToggleSnapshot, ApplyAndSave, EnumerateReverseProviders, LoadConfig, LoadColors, LoadSnapshot,
    RecreateEngine,
};
} // namespace

void Nav(Host& host, State* st, SettingsNav::NavEvent ev)
{
    dime_settings_driver_nav(&kVtbl, &host, st, &ev);
}

void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev)
{
    dime_settings_driver_frame(&kVtbl, &host, st, &ev);
}

void SwitchMode(Host& host, State* st, int mode)
{
    dime_settings_driver_switch_mode(&kVtbl, &host, st, mode);
}

void Rebuild(Host& host, State* st)
{
    dime_settings_driver_rebuild(&kVtbl, &host, st);
}

} // namespace Rust

namespace Impl = Rust;

void Nav(Host& host, State* st, SettingsNav::NavEvent ev) { Impl::Nav(host, st, ev); }
void Frame(Host& host, State* st, SettingsFrame::FrameEvent ev) { Impl::Frame(host, st, ev); }
void SwitchMode(Host& host, State* st, int mode) { Impl::SwitchMode(host, st, mode); }
void Rebuild(Host& host, State* st) { Impl::Rebuild(host, st); }

} // namespace SettingsDriver

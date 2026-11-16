// SettingsProc.cpp — see SettingsProc.h. Cpp::Main / Content are the message
// switches that used to be SettingsWindow::WndProc / ContentWndProc;
// dime-settings (wndproc.rs) mirrors them.

#include <windows.h>
#include <windowsx.h>
#include "..\BaseStructure.h"
#include "SettingsProc.h"
#include "SettingsNav.h"
#include "SettingsLifecycle.h"
#include "SettingsEditor.h"
#include "dime_ffi.h"

namespace SettingsProc {

namespace Rust {

namespace {
// C thunks over Host. Host methods do not throw; catch anyway so nothing
// unwinds into Rust.
Host& H(void* ctx) { return *static_cast<Host*>(ctx); }

int32_t HasData(void* c) { try { return H(c).HasData() ? 1 : 0; } catch (...) { return 0; } }
int32_t HasContent(void* c) { try { return H(c).HasContent() ? 1 : 0; } catch (...) { return 0; } }
void Create(void* c, intptr_t inst) { try { H(c).Create((HINSTANCE)inst); } catch (...) {} }
void Nav(void* c, int32_t k, int32_t x, int32_t y, int32_t d, int32_t t) { try { H(c).Nav(k, x, y, d, t); } catch (...) {} }
void ThemeChanged(void* c) { try { H(c).ThemeChanged(); } catch (...) {} }
void SettingChanged(void* c) { try { H(c).SettingChanged(); } catch (...) {} }
void DpiChanged(void* c, uint32_t dpi, const int32_t* rc) { try { H(c).DpiChanged(dpi, (const RECT*)rc); } catch (...) {} }
void PaintMain(void* c) { try { H(c).PaintMain(); } catch (...) {} }
void PaintContent(void* c) { try { H(c).PaintContent(); } catch (...) {} }
uint32_t ScreenDpi(void* c) { try { return H(c).ScreenDpi(); } catch (...) { return 96; } }
void SwitchMode(void* c, int32_t m) { try { H(c).SwitchMode(m); } catch (...) {} }
void SetForeground(void* c) { try { H(c).SetForeground(); } catch (...) {} }
void Destroy(void* c) { try { H(c).Destroy(); } catch (...) {} }
void Quit(void* c) { try { H(c).Quit(); } catch (...) {} }
void Command(void* c, uintptr_t w, intptr_t l) { try { H(c).Command((WPARAM)w, (LPARAM)l); } catch (...) {} }
void EditorRevalidate(void* c) { try { H(c).EditorRevalidate(); } catch (...) {} }
void EditorViewport(void* c) { try { H(c).EditorViewport(); } catch (...) {} }
int32_t DrawItem(void* c, intptr_t dis)
{
    try { return H(c).DrawItem((const DRAWITEMSTRUCT*)dis) ? 1 : 0; } catch (...) { return 0; }
}
int32_t CtlColor(void* c, intptr_t hdc, intptr_t* brush)
{
    try {
        LRESULT b = 0;
        bool handled = H(c).CtlColor((HDC)hdc, b);
        *brush = (intptr_t)b;
        return handled ? 1 : 0;
    } catch (...) { return 0; }
}
intptr_t Def(void* c, uint32_t msg, uintptr_t w, intptr_t l)
{
    try { return (intptr_t)H(c).Default(msg, (WPARAM)w, (LPARAM)l); } catch (...) { return 0; }
}

// Order must match DimeSettingsProcHostVtbl (dime_ffi.h) and ProcHostVtbl
// (settings_window.rs).
const DimeSettingsProcHostVtbl kVtbl = {
    HasData, HasContent, Create, Nav, ThemeChanged, SettingChanged, DpiChanged,
    PaintMain, PaintContent, ScreenDpi, SwitchMode, SetForeground, Destroy, Quit,
    Command, EditorRevalidate, EditorViewport, DrawItem, CtlColor, Def,
};
} // namespace

LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return (LRESULT)dime_settings_main_proc(&kVtbl, &host, msg, (uintptr_t)wParam, (intptr_t)lParam);
}

LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return (LRESULT)dime_settings_content_proc(&kVtbl, &host, msg, (uintptr_t)wParam, (intptr_t)lParam);
}

} // namespace Rust

namespace Impl = Rust;

LRESULT Main(Host& host, UINT msg, WPARAM wParam, LPARAM lParam) { return Impl::Main(host, msg, wParam, lParam); }
LRESULT Content(Host& host, UINT msg, WPARAM wParam, LPARAM lParam) { return Impl::Content(host, msg, wParam, lParam); }

} // namespace SettingsProc

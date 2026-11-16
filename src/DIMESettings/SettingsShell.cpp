// SettingsShell.cpp — see SettingsShell.h. The Cpp functions are what used to
// be in SettingsWindow.cpp (window placement, theme brushes, the WM_PAINT
// double buffering, class registration, Run); dime-settings (shell.rs)
// mirrors them.

#include <windows.h>
#include <gdiplus.h>
#include "..\BaseStructure.h"
#include "SettingsShell.h"
#include "SettingsFrame.h"
#include "dime_ffi.h"

#pragma comment(lib, "gdiplus.lib")

namespace SettingsShell {

static_assert(sizeof(DimeSettingsPaintSource) == sizeof(PaintSource), "DimeSettingsPaintSource");
static_assert(sizeof(DimeSettingsRunParams) == sizeof(RunParams), "DimeSettingsRunParams");
static_assert(sizeof(Brushes) == 3 * sizeof(void*), "Brushes");

namespace Rust {

static const DimeSettingsPaintSource* Src(const PaintSource& s)
{
    return reinterpret_cast<const DimeSettingsPaintSource*>(&s);
}

bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value)
{
    return dime_settings_save_placement(hWnd, (const uint16_t*)key, (const uint16_t*)value) != 0;
}
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value)
{
    return dime_settings_restore_placement(hWnd, (const uint16_t*)key, (const uint16_t*)value) != 0;
}
void DeletePlacement(const wchar_t* key, const wchar_t* value)
{
    dime_settings_delete_placement((const uint16_t*)key, (const uint16_t*)value);
}
Brushes CreateBrushes(const ThemeColors& theme)
{
    Brushes b = {};
    dime_settings_create_brushes(&theme, (void**)&b);
    return b;
}
void DestroyBrushes(Brushes& b) { dime_settings_destroy_brushes((void**)&b); }
void PaintMain(HWND hWnd, const PaintSource& src) { dime_settings_shell_paint_main(hWnd, Src(src)); }
void PaintContent(HWND hWnd, const PaintSource& src) { dime_settings_shell_paint_content(hWnd, Src(src)); }
void PaintMainInto(HDC target, int w, int h, const PaintSource& src)
{
    dime_settings_shell_paint_main_into(target, w, h, Src(src));
}
void PaintContentInto(HDC target, int w, int h, const PaintSource& src)
{
    dime_settings_shell_paint_content_into(target, w, h, Src(src));
}
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId)
{
    return dime_settings_register_main_class(instance, (const uint16_t*)name, (const void*)proc, iconId) != 0;
}
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc)
{
    dime_settings_register_content_class(instance, (const uint16_t*)name, (const void*)proc);
}
int Run(const RunParams& p)
{
    return dime_settings_shell_run(reinterpret_cast<const DimeSettingsRunParams*>(&p));
}

} // namespace Rust

namespace Impl = Rust;

bool SavePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value) { return Impl::SavePlacement(hWnd, key, value); }
bool RestorePlacement(HWND hWnd, const wchar_t* key, const wchar_t* value) { return Impl::RestorePlacement(hWnd, key, value); }
void DeletePlacement(const wchar_t* key, const wchar_t* value) { Impl::DeletePlacement(key, value); }
Brushes CreateBrushes(const ThemeColors& theme) { return Impl::CreateBrushes(theme); }
void DestroyBrushes(Brushes& b) { Impl::DestroyBrushes(b); }
void PaintMain(HWND hWnd, const PaintSource& src) { Impl::PaintMain(hWnd, src); }
void PaintContent(HWND hWnd, const PaintSource& src) { Impl::PaintContent(hWnd, src); }
void PaintMainInto(HDC target, int w, int h, const PaintSource& src) { Impl::PaintMainInto(target, w, h, src); }
void PaintContentInto(HDC target, int w, int h, const PaintSource& src) { Impl::PaintContentInto(target, w, h, src); }
bool RegisterMainClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, int iconId)
{
    return Impl::RegisterMainClass(instance, name, proc, iconId);
}
void RegisterContentClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc)
{
    Impl::RegisterContentClass(instance, name, proc);
}
int Run(const RunParams& p) { return Impl::Run(p); }

} // namespace SettingsShell

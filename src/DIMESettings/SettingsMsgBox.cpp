// SettingsMsgBox.cpp — ThemedMessageBox. See SettingsMsgBox.h. The Cpp:: code
// is the window that used to live in SettingsWindow.cpp; dime-settings
// (msgboxwin.rs) mirrors it.

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <strsafe.h>
#include "..\define.h"
#include "..\BaseStructure.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "SettingsMsgBox.h"
#include "SettingsWindowLayout.h"
#include "SettingsWindowPaint.h"
#include "dime_ffi.h"
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace SettingsMsgBox {

static uint32_t DpiForWindowRaw(void* hWnd) { return CConfig::GetDpiForHwnd(static_cast<HWND>(hWnd)); }

int Show(HWND hParent, const wchar_t* text, const wchar_t* caption, UINT type)
{
    bool isDark = CConfig::IsSystemDarkTheme();
    ThemeColors tc = ComputeThemeColors(isDark);
    return dime_settings_message_box(hParent, reinterpret_cast<const uint16_t*>(text),
        reinterpret_cast<const uint16_t*>(caption), type, reinterpret_cast<const uint16_t*>(kClassName),
        isDark ? 1 : 0, &tc, &DpiForWindowRaw);
}

} // namespace SettingsMsgBox

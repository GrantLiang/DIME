/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//#define DEBUG_PRINT
#include <windowsx.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <MLang.h>
#include <shellscalingapi.h>
#include "Globals.h"
#include "DIME.h"
#include "File.h"
#include "TableDictionaryEngine.h"
#include "TfInputProcessorProfile.h"
#include "Aclapi.h"
#include "CompositionProcessorEngine.h"
#ifndef DIMESettings
#include "UIPresenter.h"
#endif
#include <string>
#include <vector>
#include <dwmapi.h>
#include <uxtheme.h>
#include "dime_ffi.h"
static_assert(sizeof(DimeConfigValues) == 308, "DimeConfigValues must match dime_config::ConfigValues");

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif


#pragma comment(lib, "Shlwapi.lib")

// The settings live in Rust (dime_settings_global()). The first use sets them to
// the static initialisers (dime_config::ConfigValues::initial), which mirror the
// C++ initial values given to DIME_CONFIG_DEFINE below.
static DimeConfigEnv CurrentConfigEnv()
{
	DimeConfigEnv env = { GetSysColor(COLOR_3DHIGHLIGHT), Global::isWindows1809OrLater ? 1 : 0 };
	return env;
}

static DimeConfigValues& RustConfigValues()
{
	static DimeConfigValues* values = []() {
		DimeConfigEnv env = CurrentConfigEnv();
		dime_settings_global_init(&env);
		return dime_settings_values_ptr(dime_settings_global());
	}();
	return *values;
}

// type& CConfig::name refers to the matching DimeConfigValues field (same size and
// representation: BOOL/enum <-> int32_t, UINT/COLORREF <-> uint32_t, GUID <-> 16 bytes).
#define DIME_CONFIG_DEFINE(type, name, field, init) \
	static_assert(sizeof(type) == sizeof(RustConfigValues().field), #name " size"); \
	type& CConfig::name = reinterpret_cast<type&>(RustConfigValues().field)

//static configuration settings initialization
IME_MODE CConfig::_imeMode = IME_MODE::IME_MODE_NONE;
DIME_CONFIG_DEFINE(BOOL, _loadTableMode, load_table_mode, FALSE);
DIME_CONFIG_DEFINE(ARRAY_SCOPE, _arrayScope, array_scope, ARRAY_SCOPE::ARRAY30_UNICODE_EXT_A);
DIME_CONFIG_DEFINE(NUMERIC_PAD, _numericPad, numeric_pad, NUMERIC_PAD::NUMERIC_PAD_NUMERIC);
DIME_CONFIG_DEFINE(BOOL, _clearOnBeep, clear_on_beep, TRUE);
DIME_CONFIG_DEFINE(BOOL, _doBeep, do_beep, TRUE);
DIME_CONFIG_DEFINE(BOOL, _doBeepNotify, do_beep_notify, TRUE);
DIME_CONFIG_DEFINE(BOOL, _doBeepOnCandi, do_beep_on_candi, FALSE);
DIME_CONFIG_DEFINE(BOOL, _sentenceMode, sentence_mode, FALSE);
DIME_CONFIG_DEFINE(BOOL, _autoCompose, auto_compose, FALSE);
DIME_CONFIG_DEFINE(BOOL, _customTablePriority, custom_table_priority, FALSE);
DIME_CONFIG_DEFINE(BOOL, _arrayForceSP, array_force_sp, FALSE);
DIME_CONFIG_DEFINE(BOOL, _arrayNotifySP, array_notify_sp, TRUE);
DIME_CONFIG_DEFINE(BOOL, _arrowKeySWPages, arrow_key_sw_pages, TRUE);
DIME_CONFIG_DEFINE(BOOL, _spaceAsPageDown, space_as_page_down, FALSE);
DIME_CONFIG_DEFINE(BOOL, _spaceAsFirstCandSelkey, space_as_first_cand_selkey, FALSE);
DIME_CONFIG_DEFINE(UINT, _fontSize, font_size, 12);
DIME_CONFIG_DEFINE(UINT, _fontWeight, font_weight, FW_NORMAL);
DIME_CONFIG_DEFINE(BOOL, _fontItalic, font_italic, FALSE);
DIME_CONFIG_DEFINE(UINT, _maxCodes, max_codes, 4);
DIME_CONFIG_DEFINE(BOOL, _appPermissionSet, app_permission_set, FALSE);
DIME_CONFIG_DEFINE(BOOL, _activatedKeyboardMode, activated_keyboard_mode, TRUE);
DIME_CONFIG_DEFINE(BOOL, _makePhrase, make_phrase, TRUE);
DIME_CONFIG_DEFINE(BOOL, _doHanConvert, do_han_convert, FALSE);
DIME_CONFIG_DEFINE(BOOL, _showNotifyDesktop, show_notify_desktop, TRUE);
DIME_CONFIG_DEFINE(BOOL, _dayiArticleMode, dayi_article_mode, FALSE);  // Article mode: input full-shaped symbols with address keys
DIME_CONFIG_DEFINE(DAYI_SELKEY_MODE, _dayiSelkeyMode, dayi_selkey_mode, DAYI_SELKEY_MODE::DAYI_SELKEY_FROM_TABLE);
BOOL CConfig::_customTableChanged = FALSE;
DIME_CONFIG_DEFINE(BOOL, _arraySingleQuoteCustomPhrase, array_single_quote_custom_phrase, FALSE);
DIME_CONFIG_DEFINE(BOOL, _big5Filter, big5_filter, FALSE);

UINT CConfig::_dpiY = 0;
_T_GetDpiForMonitor CConfig::_GetDpiForMonitor = nullptr;
_T_GetSystemMetricsForDpi CConfig::_GetSystemMetricsForDpi = nullptr;
_T_SetThreadDpiAwarenessContext CConfig::_SetThreadDpiAwarenessContext = nullptr;

UINT CConfig::GetDpiForHwnd(HWND hWnd)
{
#ifdef DIME_UNIT_TESTING
	if (testDpiOverride) return testDpiOverride;
#endif
	if (hWnd && _GetDpiForMonitor)
	{
		HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		UINT dpiX, dpiY;
		if (SUCCEEDED(_GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiY > 0)
			return dpiY;
	}
	HDC hDC = GetDC(nullptr);
	UINT dpi = (UINT)GetDeviceCaps(hDC, LOGPIXELSY);
	ReleaseDC(nullptr, hDC);
	return dpi;
}

DIME_CONFIG_DEFINE(PHONETIC_KEYBOARD_LAYOUT, _phoneticKeyboardLayout, phonetic_keyboard_layout, PHONETIC_KEYBOARD_LAYOUT::PHONETIC_STANDARD_KEYBOARD_LAYOUT);
DIME_CONFIG_DEFINE(IME_SHIFT_MODE, _imeShiftMode, ime_shift_mode, IME_SHIFT_MODE::IME_BOTH_SHIFT);
DIME_CONFIG_DEFINE(DOUBLE_SINGLE_BYTE_MODE, _doubleSingleByteMode, double_single_byte_mode, DOUBLE_SINGLE_BYTE_MODE::DOUBLE_SINGLE_BYTE_ALWAYS_SINGLE);
DIME_CONFIG_DEFINE(IME_SHIFT_ENGLISH_DIGIT_MODE, _shiftEnglishDigitMode, shift_english_digit_mode, IME_SHIFT_ENGLISH_DIGIT_MODE::SHIFT_ENGLISH_DIGIT_CAPS_OFF_SYMBOL);

CDIMEArray <LanguageProfileInfo>* CConfig::_reverseConvervsionInfoList = new (std::nothrow) CDIMEArray <LanguageProfileInfo>;
DIME_CONFIG_DEFINE(CLSID, _reverseConverstionCLSID, reverse_conversion_clsid, CLSID_NULL);
DIME_CONFIG_DEFINE(GUID, _reverseConversionGUIDProfile, reverse_conversion_guid_profile, CLSID_NULL);
WCHAR CConfig::_pwzsDIMEProfile[] = L"\0";
WCHAR CConfig::_pwszINIFileName[] = L"\0";
IME_MODE CConfig::_configIMEMode = IME_MODE::IME_MODE_NONE;
DIME_CONFIG_DEFINE(BOOL, _reloadReverseConversion, reload_reverse_conversion, FALSE);

DIME_CONFIG_DEFINE(DimeFontFaceName, _pFontFaceName, font_face_name, { L"微軟正黑體" });
WCHAR CConfig::_pMlFontFaceName[] = { L"細明體" };

DIME_CONFIG_DEFINE(COLORREF, _itemColor, custom.item, CANDWND_ITEM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _itemBGColor, custom.item_bg, GetSysColor(COLOR_3DHIGHLIGHT));
DIME_CONFIG_DEFINE(COLORREF, _selectedColor, custom.selected, CANDWND_SELECTED_ITEM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _selectedBGColor, custom.selected_bg, CANDWND_SELECTED_BK_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _phraseColor, custom.phrase, CANDWND_PHRASE_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _numberColor, custom.number, CANDWND_NUM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _lightItemColor, light.item, CANDWND_ITEM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _lightItemBGColor, light.item_bg, GetSysColor(COLOR_3DHIGHLIGHT));
DIME_CONFIG_DEFINE(COLORREF, _lightSelectedColor, light.selected, CANDWND_SELECTED_ITEM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _lightSelectedBGColor, light.selected_bg, CANDWND_SELECTED_BK_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _lightPhraseColor, light.phrase, CANDWND_PHRASE_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _lightNumberColor, light.number, CANDWND_NUM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _darkItemColor, dark.item, CANDWND_DARK_ITEM_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _darkItemBGColor, dark.item_bg, CANDWND_DARK_ITEM_BG_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _darkSelectedColor, dark.selected, CANDWND_DARK_SELECTED_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _darkSelectedBGColor, dark.selected_bg, CANDWND_DARK_SELECTED_BG_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _darkPhraseColor, dark.phrase, CANDWND_DARK_PHRASE_COLOR);
DIME_CONFIG_DEFINE(COLORREF, _darkNumberColor, dark.number, CANDWND_DARK_NUM_COLOR);
DIME_CONFIG_DEFINE(IME_COLOR_MODE, _colorMode, color_mode, IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM);
DIME_CONFIG_DEFINE(BOOL, _colorModeKeyFound, color_mode_key_found, FALSE);
ColorInfo CConfig::colors[6] =
{
	{ IDC_COL_FR, CANDWND_ITEM_COLOR },
	{ IDC_COL_SEFR, CANDWND_SELECTED_ITEM_COLOR },
	{ IDC_COL_BG, GetSysColor(COLOR_3DHIGHLIGHT) },
	{ IDC_COL_PHRASE, CANDWND_PHRASE_COLOR },
	{ IDC_COL_NU, CANDWND_NUM_COLOR },
	{ IDC_COL_SEBG, CANDWND_SELECTED_BK_COLOR }
};

struct _stat CConfig::_initTimeStamp = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; //zero the timestamp


// Load colors[] from the palette for the given mode.
void CConfig::LoadColorsForMode(IME_COLOR_MODE mode)
{
	switch (mode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:
		colors[0].color = _darkItemColor;
		colors[1].color = _darkSelectedColor;
		colors[2].color = _darkItemBGColor;
		colors[3].color = _darkPhraseColor;
		colors[4].color = _darkNumberColor;
		colors[5].color = _darkSelectedBGColor;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM:
		colors[0].color = _itemColor;
		colors[1].color = _selectedColor;
		colors[2].color = _itemBGColor;
		colors[3].color = _phraseColor;
		colors[4].color = _numberColor;
		colors[5].color = _selectedBGColor;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
		if (IsSystemDarkTheme())
		{
			colors[0].color = _darkItemColor;
			colors[1].color = _darkSelectedColor;
			colors[2].color = _darkItemBGColor;
			colors[3].color = _darkPhraseColor;
			colors[4].color = _darkNumberColor;
			colors[5].color = _darkSelectedBGColor;
		}
		else
		{
			colors[0].color = _lightItemColor;
			colors[1].color = _lightSelectedColor;
			colors[2].color = _lightItemBGColor;
			colors[3].color = _lightPhraseColor;
			colors[4].color = _lightNumberColor;
			colors[5].color = _lightSelectedBGColor;
		}
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:
	default:
		colors[0].color = _lightItemColor;
		colors[1].color = _lightSelectedColor;
		colors[2].color = _lightItemBGColor;
		colors[3].color = _lightPhraseColor;
		colors[4].color = _lightNumberColor;
		colors[5].color = _lightSelectedBGColor;
		break;
	}
}

// Save colors[] back to the palette for the given mode.
void CConfig::SaveColorsForMode(IME_COLOR_MODE mode)
{
	switch (mode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:
		_darkItemColor     = colors[0].color;
		_darkSelectedColor = colors[1].color;
		_darkItemBGColor   = colors[2].color;
		_darkPhraseColor   = colors[3].color;
		_darkNumberColor   = colors[4].color;
		_darkSelectedBGColor = colors[5].color;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM:
		_itemColor         = colors[0].color;
		_selectedColor     = colors[1].color;
		_itemBGColor       = colors[2].color;
		_phraseColor       = colors[3].color;
		_numberColor       = colors[4].color;
		_selectedBGColor   = colors[5].color;
		break;
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
		break; // color boxes disabled in SYSTEM mode — nothing to save
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:
	default:
		_lightItemColor     = colors[0].color;
		_lightSelectedColor = colors[1].color;
		_lightItemBGColor   = colors[2].color;
		_lightPhraseColor   = colors[3].color;
		_lightNumberColor   = colors[4].color;
		_lightSelectedBGColor = colors[5].color;
		break;
	}
}

#ifdef DIME_UNIT_TESTING
int CConfig::testSystemDarkOverride = -1;
UINT CConfig::testDpiOverride = 0;
#endif

// Detect Windows dark theme (safe on Win7/8/10/11)
bool CConfig::IsSystemDarkTheme()
{
#ifdef DIME_UNIT_TESTING
	if (testSystemDarkOverride >= 0) return testSystemDarkOverride != 0;
#endif
	// Get OS build number once using RtlGetVersion
	static const DWORD build = []() -> DWORD {
		typedef NTSTATUS (WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
		HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
		if (!hNtdll) return 0;
		auto pFn = reinterpret_cast<RtlGetVersionFn>(
			GetProcAddress(hNtdll, "RtlGetVersion")
		);
		RTL_OSVERSIONINFOW osvi = { sizeof(osvi) };
		return (pFn && pFn(&osvi) == 0) ? osvi.dwBuildNumber : 0;
	}();

	// On Win10 1809+ (build >= 17763): use ShouldAppsUseDarkMode API
	if (build >= 17763)
	{
		typedef bool (WINAPI* FnShouldAppsUseDarkMode)();
		HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (hUxtheme) {
			auto pFn = reinterpret_cast<FnShouldAppsUseDarkMode>(
				GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132))
			);
			if (pFn) return pFn();
		}
	}

	// Registry fallback (Win7/8/pre-1809 always land here)
	DWORD lightTheme = 1;
	DWORD cb = sizeof(lightTheme);
	RegGetValueW(HKEY_CURRENT_USER,
		L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme",
		RRF_RT_REG_DWORD, nullptr, &lightTheme, &cb);
	return lightTheme == 0;
}

BOOL CConfig::GetAlwaysShowScrollbars()
{
	DWORD dynamicSB = 1; // default: auto-hide (always show = FALSE)
	DWORD cb = sizeof(dynamicSB);
	RegGetValueW(HKEY_CURRENT_USER,
		L"Control Panel\\Accessibility",
		L"DynamicScrollbars",
		RRF_RT_REG_DWORD, nullptr, &dynamicSB, &cb);
	return (dynamicSB == 0); // 0 = always show; 1 or missing = auto-hide
}

bool CConfig::GetEffectiveDarkMode()
{
	switch (_colorMode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:  return false;
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:   return true;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM: return false; // custom uses user colors, no dark preset
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
	default:
		return IsSystemDarkTheme();
	}
}

COLORREF CConfig::GetActiveItemBGColor()
{
	switch (_colorMode)
	{
	case IME_COLOR_MODE::IME_COLOR_MODE_DARK:   return _darkItemBGColor;
	case IME_COLOR_MODE::IME_COLOR_MODE_LIGHT:  return _lightItemBGColor;
	case IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM: return _itemBGColor;
	case IME_COLOR_MODE::IME_COLOR_MODE_SYSTEM:
	default:
		return IsSystemDarkTheme() ? _darkItemBGColor : _lightItemBGColor;
	}
}

//+---------------------------------------------------------------------------
//
// Rust bridge (rust/crates/dime-config)
//
// The static members remain the storage. Rust logic runs on a snapshot taken
// with CreateRustSettings() and is copied back with ImportRustSettings().
//
//----------------------------------------------------------------------------

static void ToPalette(DimePalette& p, COLORREF item, COLORREF phrase, COLORREF number, COLORREF itemBG, COLORREF selected, COLORREF selectedBG)
{
	p.item = item; p.phrase = phrase; p.number = number;
	p.item_bg = itemBG; p.selected = selected; p.selected_bg = selectedBG;
}

void CConfig::ExportValues(_Out_ DimeConfigValues* v)
{
	ZeroMemory(v, sizeof(*v));
	v->auto_compose = _autoCompose;
	v->space_as_page_down = _spaceAsPageDown;
	v->space_as_first_cand_selkey = _spaceAsFirstCandSelkey;
	v->arrow_key_sw_pages = _arrowKeySWPages;
	v->clear_on_beep = _clearOnBeep;
	v->do_beep = _doBeep;
	v->do_beep_notify = _doBeepNotify;
	v->do_beep_on_candi = _doBeepOnCandi;
	v->sentence_mode = _sentenceMode;
	v->activated_keyboard_mode = _activatedKeyboardMode;
	v->make_phrase = _makePhrase;
	v->show_notify_desktop = _showNotifyDesktop;
	v->do_han_convert = _doHanConvert;
	v->font_italic = _fontItalic;
	v->custom_table_priority = _customTablePriority;
	v->dayi_article_mode = _dayiArticleMode;
	v->array_force_sp = _arrayForceSP;
	v->array_notify_sp = _arrayNotifySP;
	v->array_single_quote_custom_phrase = _arraySingleQuoteCustomPhrase;
	v->big5_filter = _big5Filter;
	v->reload_reverse_conversion = _reloadReverseConversion;
	v->app_permission_set = _appPermissionSet;
	v->load_table_mode = _loadTableMode;
	v->color_mode_key_found = _colorModeKeyFound ? 1 : 0;
	v->max_codes = _maxCodes;
	v->font_size = _fontSize;
	v->font_weight = _fontWeight;
	v->ime_shift_mode = (int32_t)_imeShiftMode;
	v->double_single_byte_mode = (int32_t)_doubleSingleByteMode;
	v->shift_english_digit_mode = (int32_t)_shiftEnglishDigitMode;
	v->color_mode = (int32_t)_colorMode;
	v->numeric_pad = (int32_t)_numericPad;
	v->array_scope = (int32_t)_arrayScope;
	v->phonetic_keyboard_layout = (int32_t)_phoneticKeyboardLayout;
	v->dayi_selkey_mode = (int32_t)_dayiSelkeyMode;
	ToPalette(v->custom, _itemColor, _phraseColor, _numberColor, _itemBGColor, _selectedColor, _selectedBGColor);
	ToPalette(v->light, _lightItemColor, _lightPhraseColor, _lightNumberColor, _lightItemBGColor, _lightSelectedColor, _lightSelectedBGColor);
	ToPalette(v->dark, _darkItemColor, _darkPhraseColor, _darkNumberColor, _darkItemBGColor, _darkSelectedColor, _darkSelectedBGColor);
	static_assert(sizeof(v->font_face_name) == sizeof(_pFontFaceName), "LF_FACESIZE");
	memcpy(v->font_face_name, _pFontFaceName, sizeof(_pFontFaceName));
	v->font_face_name[LF_FACESIZE - 1] = 0;
	static_assert(sizeof(v->reverse_conversion_clsid) == sizeof(GUID), "GUID size");
	memcpy(v->reverse_conversion_clsid, &_reverseConverstionCLSID, sizeof(GUID));
	memcpy(v->reverse_conversion_guid_profile, &_reverseConversionGUIDProfile, sizeof(GUID));
}

void CConfig::ImportValues(_In_ const DimeConfigValues* v)
{
	_autoCompose = v->auto_compose;
	_spaceAsPageDown = v->space_as_page_down;
	_spaceAsFirstCandSelkey = v->space_as_first_cand_selkey;
	_arrowKeySWPages = v->arrow_key_sw_pages;
	_clearOnBeep = v->clear_on_beep;
	_doBeep = v->do_beep;
	_doBeepNotify = v->do_beep_notify;
	_doBeepOnCandi = v->do_beep_on_candi;
	_sentenceMode = v->sentence_mode;
	_activatedKeyboardMode = v->activated_keyboard_mode;
	_makePhrase = v->make_phrase;
	_showNotifyDesktop = v->show_notify_desktop;
	_doHanConvert = v->do_han_convert;
	_fontItalic = v->font_italic;
	_customTablePriority = v->custom_table_priority;
	_dayiArticleMode = v->dayi_article_mode;
	_arrayForceSP = v->array_force_sp;
	_arrayNotifySP = v->array_notify_sp;
	_arraySingleQuoteCustomPhrase = v->array_single_quote_custom_phrase;
	_big5Filter = v->big5_filter;
	_reloadReverseConversion = v->reload_reverse_conversion;
	_appPermissionSet = v->app_permission_set;
	_loadTableMode = v->load_table_mode;
	_colorModeKeyFound = v->color_mode_key_found != 0;
	_maxCodes = v->max_codes;
	_fontSize = v->font_size;
	_fontWeight = v->font_weight;
	_imeShiftMode = (IME_SHIFT_MODE)v->ime_shift_mode;
	_doubleSingleByteMode = (DOUBLE_SINGLE_BYTE_MODE)v->double_single_byte_mode;
	_shiftEnglishDigitMode = (IME_SHIFT_ENGLISH_DIGIT_MODE)v->shift_english_digit_mode;
	_colorMode = (IME_COLOR_MODE)v->color_mode;
	_numericPad = (NUMERIC_PAD)v->numeric_pad;
	_arrayScope = (ARRAY_SCOPE)v->array_scope;
	_phoneticKeyboardLayout = (PHONETIC_KEYBOARD_LAYOUT)v->phonetic_keyboard_layout;
	_dayiSelkeyMode = (DAYI_SELKEY_MODE)v->dayi_selkey_mode;
	_itemColor = v->custom.item; _phraseColor = v->custom.phrase; _numberColor = v->custom.number;
	_itemBGColor = v->custom.item_bg; _selectedColor = v->custom.selected; _selectedBGColor = v->custom.selected_bg;
	_lightItemColor = v->light.item; _lightPhraseColor = v->light.phrase; _lightNumberColor = v->light.number;
	_lightItemBGColor = v->light.item_bg; _lightSelectedColor = v->light.selected; _lightSelectedBGColor = v->light.selected_bg;
	_darkItemColor = v->dark.item; _darkPhraseColor = v->dark.phrase; _darkNumberColor = v->dark.number;
	_darkItemBGColor = v->dark.item_bg; _darkSelectedColor = v->dark.selected; _darkSelectedBGColor = v->dark.selected_bg;
	memcpy(_pFontFaceName, v->font_face_name, sizeof(_pFontFaceName));
	_pFontFaceName[LF_FACESIZE - 1] = L'\0';
	memcpy(&_reverseConverstionCLSID, v->reverse_conversion_clsid, sizeof(GUID));
	memcpy(&_reverseConversionGUIDProfile, v->reverse_conversion_guid_profile, sizeof(GUID));
}

DimeSettings* CConfig::CreateRustSettings()
{
	DimeConfigValues values;
	ExportValues(&values);
	DimeConfigEnv env = { GetSysColor(COLOR_3DHIGHLIGHT), Global::isWindows1809OrLater ? 1 : 0 };
	DimeSettings* s = dime_settings_new(&values, &env);
	const WCHAR* description = GetReverseConversionDescription();
	if (s && description)
		dime_settings_set_description(s, reinterpret_cast<const uint16_t*>(description), wcslen(description), 1);
	return s;
}

void CConfig::ImportRustSettings(_In_ const DimeSettings* s)
{
	DimeConfigValues values;
	if (!s || !dime_settings_get_values(s, &values)) return;
	ImportValues(&values);

	const uint16_t* text = nullptr;
	size_t len = 0;
	BOOL present = dime_settings_get_description(s, &text, &len);
	const WCHAR* current = GetReverseConversionDescription();
	if (present && current
		&& wcsncmp(current, reinterpret_cast<const wchar_t*>(text), len) == 0 && current[len] == L'\0')
		return; // unchanged: keep the pointer callers may hold
	std::wstring copy(reinterpret_cast<const wchar_t*>(text), present ? len : 0);
	SetReverseConversionDescriptionText(present ? copy.c_str() : nullptr);
}

CRustConfigScope::CRustConfigScope() : _pSettings(nullptr), _commit(false)
{
	RustConfigValues(); // make sure the storage is initialised
	_pSettings = dime_settings_global();
	DimeConfigEnv env = CurrentConfigEnv();
	dime_settings_set_env(_pSettings, &env);
}

CRustConfigScope::~CRustConfigScope()
{
}

WCHAR* CConfig::GetReverseConversionDescription()
{
	RustConfigValues(); // make sure the storage is initialised
	return const_cast<WCHAR*>(reinterpret_cast<const WCHAR*>(dime_settings_description_cstr(dime_settings_global())));
}

// Run one dime_settings_* operation on the settings.
static void RunRustSettingsOp(void (*op)(DimeSettings*, int32_t), IME_MODE imeMode)
{
	CRustConfigScope scope;
	if (!scope.Get()) return;
	op(scope.Get(), (int32_t)imeMode);
	scope.Commit();
}

#ifndef DIMESettings
void CConfig::ApplyIMEColorSet(CUIPresenter* pPresenter, bool isPhraseMode)
{
	if (!pPresenter) return;

	bool dark = GetEffectiveDarkMode();

	if (_colorMode == IME_COLOR_MODE::IME_COLOR_MODE_CUSTOM)
	{
		// Use the 6 user-configured colors
		COLORREF textColor = isPhraseMode ? _phraseColor : _itemColor;
		pPresenter->_SetCandidateTextColor(textColor, _itemBGColor);
		pPresenter->_SetCandidateNumberColor(_numberColor, _itemBGColor);
		pPresenter->_SetCandidateSelectedTextColor(_selectedColor, _selectedBGColor);
		pPresenter->_SetCandidateFillColor(_itemBGColor);
	}
	else if (dark)
	{
		COLORREF textColor = isPhraseMode ? _darkPhraseColor : _darkItemColor;
		pPresenter->_SetCandidateTextColor(textColor, _darkItemBGColor);
		pPresenter->_SetCandidateNumberColor(_darkNumberColor, _darkItemBGColor);
		pPresenter->_SetCandidateSelectedTextColor(_darkSelectedColor, _darkSelectedBGColor);
		pPresenter->_SetCandidateFillColor(_darkItemBGColor);
	}
	else
	{
		// Light palette (customizable; defaults match compile-time constants)
		COLORREF textColor = isPhraseMode ? _lightPhraseColor : _lightItemColor;
		pPresenter->_SetCandidateTextColor(textColor, _lightItemBGColor);
		pPresenter->_SetCandidateNumberColor(_lightNumberColor, _lightItemBGColor);
		pPresenter->_SetCandidateSelectedTextColor(_lightSelectedColor, _lightSelectedBGColor);
		pPresenter->_SetCandidateFillColor(_lightItemBGColor);
	}
}
#endif // !DIMESettings

//+---------------------------------------------------------------------------
//
// writeConfig
//
//----------------------------------------------------------------------------

VOID CConfig::WriteConfig(IME_MODE imeMode, BOOL confirmUpdated)
{
	debugPrint(L"CConfig::WriteConfig() \n");

	struct _stat initTimeStamp;
	BOOL failed = _wstat(_pwszINIFileName, &initTimeStamp) != 0;
	BOOL updated = FALSE;
		if(!failed)
			updated = difftime(initTimeStamp.st_mtime, _initTimeStamp.st_mtime) > 0;

	if(confirmUpdated && !failed && updated)
	{
		//The config file is updated
		MessageBox(GetFocus(), L"新設定未生效!\n設定檔已被其他程式更新，請避免在兩個程式同時開啟設定頁面。",
			L"設定錯誤", MB_ICONERROR);
		LoadConfig(imeMode);
	}
	if(!confirmUpdated || failed || !updated)
	{
		DimeBuffer* text = nullptr;
		{
			CRustConfigScope scope;
			if (scope.Get()) text = dime_settings_serialize_ini(scope.Get(), (int32_t)imeMode);
		}
		if (text)
		{
			size_t len = 0;
			const uint16_t* data = dime_buffer_data(text, &len);
			FILE* fp = nullptr;
			if (_wfopen_s(&fp, _pwszINIFileName, L"wb") == 0 && fp) // overwrite the file
			{
				fwrite(data, sizeof(uint16_t), len, fp);
				fclose(fp);
			}
			dime_buffer_free(text);
		}
	}


}

void CConfig::SetIMEMode(IME_MODE imeMode)
{
	if (_imeMode != imeMode)
	{
		_imeMode = imeMode;
		GUID guidProfile = CLSID_NULL;
		WCHAR wszAppData[MAX_PATH] = L"\0";
		Global::GetAppDataPath(wszAppData, TRUE);

		StringCchPrintf(_pwzsDIMEProfile, MAX_PATH, L"%s\\DIME", wszAppData);
		if (!PathFileExists(_pwzsDIMEProfile))
		{
			if (CreateDirectory(_pwzsDIMEProfile, NULL) == 0) return;
		}

		if (!PathFileExists(_pwzsDIMEProfile))
		{   //DIME roaming profile is not exist. Create one.
			if (CreateDirectory(_pwzsDIMEProfile, NULL) == 0) return;
		}
		if (imeMode == IME_MODE::IME_MODE_DAYI)
		{
			guidProfile = Global::DIMEDayiGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\DayiConfig.ini", _pwzsDIMEProfile);
		}
		else if (imeMode == IME_MODE::IME_MODE_ARRAY)
		{
			guidProfile = Global::DIMEArrayGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\ArrayConfig.ini", _pwzsDIMEProfile);
		}
		else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
		{
			guidProfile = Global::DIMEPhoneticGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\PhoneConfig.ini", _pwzsDIMEProfile);
		}
		else if (imeMode == IME_MODE::IME_MODE_GENERIC)
		{
			guidProfile = Global::DIMEGenericGuidProfile;
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\GenericConfig.ini", _pwzsDIMEProfile);
		}
		else
			StringCchPrintf(_pwszINIFileName, MAX_PATH, L"%s\\config.ini", _pwzsDIMEProfile);


		// filter out self from reverse conversion list
		for (UINT i = 0; i < _reverseConvervsionInfoList->Count(); i++)
		{
			if (IsEqualCLSID(_reverseConvervsionInfoList->GetAt(i)->guidProfile, guidProfile))
				_reverseConvervsionInfoList->RemoveAt(i);
		}
	}
	return;

}

//+---------------------------------------------------------------------------
//
// loadConfig
//
//----------------------------------------------------------------------------

//+---------------------------------------------------------------------------
//
// SetIMEModeDefaults
//
// Set IME-specific default values based on mode
// This ensures corrupted config values fall back to appropriate defaults
//
//----------------------------------------------------------------------------
void CConfig::SetIMEModeDefaults(IME_MODE imeMode)
{
	RunRustSettingsOp(dime_settings_set_mode_defaults, imeMode);
}

//+---------------------------------------------------------------------------
//
// ResetAllDefaults
//
// Reset every config setting to its initial (C++ static initialiser) value,
// then apply mode-specific overrides via SetIMEModeDefaults().
// Used by the CLI --reset command so that global settings like FontSize are
// also restored even when a config file already exists on disk.
//
//----------------------------------------------------------------------------
void CConfig::ResetAllDefaults(IME_MODE imeMode)
{
	RunRustSettingsOp(dime_settings_reset_all_defaults, imeMode);
}

//+---------------------------------------------------------------------------
//
// LoadConfigFromFile
//
// The parsing part of LoadConfig: IME-specific defaults first (valid config
// values override them; corrupted values are skipped, keeping the defaults),
// then the [Config] entries, then the legacy ColorMode migration.
// Returns FALSE when the file cannot be opened (only the defaults are applied).
//
//----------------------------------------------------------------------------

BOOL CConfig::LoadConfigFromFile(IME_MODE imeMode, _In_ LPCWSTR pwszINIFileName)
{
	SetIMEModeDefaults(imeMode);

	CFile* iniDictionaryFile = new (std::nothrow) CFile();
	if (!iniDictionaryFile || !iniDictionaryFile->CreateFile(pwszINIFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ | FILE_SHARE_WRITE))
	{
		delete iniDictionaryFile;
		return FALSE;
	}

	CTableDictionaryEngine* iniTableDictionaryEngine = CTableDictionaryEngine::Create(MAKELCID(1028, SORT_DEFAULT), iniDictionaryFile, DICTIONARY_TYPE::INI_DICTIONARY);//CHT:1028
	if (iniTableDictionaryEngine)
	{
		// _loadTableMode = TRUE, _colorModeKeyFound = false, _colorMode = SYSTEM
		RunRustSettingsOp(dime_settings_begin_load, imeMode);
		iniTableDictionaryEngine->ParseConfig(imeMode); //parse config first.
		debugPrint(L"CConfig::LoadConfigFromFile() parsed. _loadTableMode = %d\n", _loadTableMode);

		// Backward compat: old INI has no ColorMode key but may have the
		// legacy 6 color keys (ItemColor etc.) from before Light/Dark/Custom
		// palettes were introduced.  If the user had customized those colors,
		// migrate to CUSTOM to preserve them.  If still factory defaults,
		// migrate to SYSTEM (or LIGHT on pre-1809 Windows).
		RunRustSettingsOp([](DimeSettings* s, int32_t) { dime_settings_finish_load(s); }, imeMode);
	}
	delete iniTableDictionaryEngine; // delete after config.ini config are parsed
	delete iniDictionaryFile;
	return TRUE;
}

//+---------------------------------------------------------------------------
//
// LoadConfig
//
//----------------------------------------------------------------------------

BOOL CConfig::LoadConfig(IME_MODE imeMode)
{
	debugPrint(L"CConfig::loadConfig() \n");
	SetIMEMode (imeMode);

	PACL pOldDACL = NULL, pNewDACL = NULL;
	PSECURITY_DESCRIPTOR pSD = NULL;
	BOOL bRET = FALSE;

	if (PathFileExists(_pwszINIFileName))
	{
		debugPrint(L"CConfig::loadConfig() config file = %s exists\n", _pwszINIFileName);
		struct _stat initTimeStamp;
		BOOL failed = _wstat(_pwszINIFileName, &initTimeStamp) != 0;  //error for retrieving timestamp
		BOOL updated = FALSE;
		if (!failed)
			updated = difftime(initTimeStamp.st_mtime, _initTimeStamp.st_mtime) > 0;

		debugPrint(L"CConfig::loadConfig() wstat failed = %d, config file updated = %d\n", failed, updated);
		if (failed || updated || _configIMEMode!=imeMode)
		{
			bRET = TRUE;

			if (LoadConfigFromFile(imeMode, _pwszINIFileName))
			{
				SetDefaultTextFont();
				_initTimeStamp.st_mtime = initTimeStamp.st_mtime;
				_configIMEMode = imeMode;
			}

			// In store app mode, the dll is loaded into app container which does not even have read right for IME profile in APPDATA.
			// Here, the read right is granted once to "ALL APPLICATION PACKAGES" when loaded in desktop mode, so as all metro apps can at least read the user settings in config.ini.
#ifdef DIMESettings
			if (!_appPermissionSet && imeMode != IME_MODE::IME_MODE_NONE)
#else
			if (!CDIME::_IsStoreAppMode() && !_appPermissionSet && imeMode != IME_MODE::IME_MODE_NONE)
#endif
			{
				EXPLICIT_ACCESS ea;
				// Get a pointer to the existing DACL (Conditionally).
				DWORD dwRes = GetNamedSecurityInfo(_pwzsDIMEProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);
				if (ERROR_SUCCESS != dwRes) goto ErrorExit;
				// Initialize an EXPLICIT_ACCESS structure for the new ACE.
				ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));
				ea.grfAccessPermissions = GENERIC_READ;
				ea.grfAccessMode = GRANT_ACCESS;
				ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
				ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
				ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
				ea.Trustee.ptstrName = (LPWCH) L"Everyone";
				// Create a new ACL that merges the new ACE into the existing DACL.
				dwRes = SetEntriesInAcl(1, &ea, pOldDACL, &pNewDACL);
				if (ERROR_SUCCESS != dwRes) goto ErrorExit;
				if (Global::isWindows8)
				{
					ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
					ea.Trustee.ptstrName = (LPWCH) L"ALL APPLICATION PACKAGES";
					dwRes = SetEntriesInAcl(1, &ea, pNewDACL, &pNewDACL);
					if (ERROR_SUCCESS != dwRes) goto ErrorExit;
				}
				if (pNewDACL)
					SetNamedSecurityInfo(_pwzsDIMEProfile, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pNewDACL, NULL);

				_appPermissionSet = TRUE;
				WriteConfig(imeMode, FALSE);
			}
		}
	}
	else
	{
		// Config file doesn't exist - set IME-specific defaults
		// Use SYSTEM only on Win10 1809+; fall back to LIGHT on older Windows
		// so the combo shows the correct selection when settings first open.
		RunRustSettingsOp(dime_settings_load_missing, imeMode);

		if (imeMode != IME_MODE::IME_MODE_NONE)
			WriteConfig(imeMode, FALSE); // config.ini is not there. create one.
	}


ErrorExit:
	if (pNewDACL != NULL)
		LocalFree(pNewDACL);
	if (pSD != NULL)
		LocalFree(pSD);
	return bRET;  // return TRUE if the config file updated
}

void CConfig::SetDefaultTextFont(HWND hWnd)
{
	debugPrint(L"CConfig::SetDefaultTextFont()");
	UINT dpiY = 0;
	if (hWnd && _GetDpiForMonitor)
	{
		HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		UINT dpiX;
		_GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
	}

	if (Global::defaultlFontHandle != nullptr && (hWnd == nullptr || ( Global::isWindows8 && _dpiY != dpiY) ))
	{
		debugPrint(L"CConfig::SetDefaultTextFont() delete old font");
		DeleteObject((HGDIOBJ)Global::defaultlFontHandle);
		Global::defaultlFontHandle = nullptr;
	}
	if (Global::defaultlFontHandle == nullptr)
	{
		HDC hDC = GetDC(nullptr);
		int logPixelY = GetDeviceCaps(hDC, LOGPIXELSY);
		if (Global::isWindows8 && dpiY > 0)
		{
			logPixelY = dpiY;
			_dpiY = dpiY;
		}
		int logFontSize = -MulDiv(_fontSize, logPixelY, 72);
		debugPrint(L"CConfig::SetDefaultTextFont() create new font. Logical y pixels = %d, font size =%d,  log font size = %d, _dpiY = %d",
			logPixelY, _fontSize, logFontSize, dpiY);

		Global::defaultlFontHandle = CreateFont(logFontSize, 0, 0, 0, _fontWeight, _fontItalic, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, _pFontFaceName);
		if (!Global::defaultlFontHandle)
		{
			LOGFONT lf = { 0 };
			SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
			// Fall back to the default GUI font on failure.
			Global::defaultlFontHandle = CreateFont(-MulDiv(_fontSize, GetDeviceCaps(hDC, LOGPIXELSY), 72),
				0, 0, 0, FW_MEDIUM, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, lf.lfFaceName);
		}
		if(hDC) ReleaseDC(nullptr, hDC);
	}

}


void CConfig::SetReverseConvervsionInfoList(CDIMEArray <LanguageProfileInfo> *reverseConvervsionInfoList)
{
	// clear _reverseConvervsionInfoList first.
	clearReverseConvervsionInfoList();

	for (UINT index = 0; index < reverseConvervsionInfoList->Count(); index++)
	{
		LanguageProfileInfo *infoItem = nullptr;
		infoItem = reverseConvervsionInfoList->GetAt(index);

		LanguageProfileInfo *_infoItem = nullptr;
		_infoItem = _reverseConvervsionInfoList->Append();
		_infoItem->clsid = infoItem->clsid;
		_infoItem->guidProfile = infoItem->guidProfile;
		PWCH _description = new (std::nothrow) WCHAR[wcslen(infoItem->description) + 1];
		if (_description)
		{
			*_description = L'\0';
			StringCchCopy(_description, wcslen(infoItem->description) + 1, infoItem->description);
			_infoItem->description = _description;
		}
	}
}

void CConfig::clearReverseConvervsionInfoList()
{
	for (UINT index = 0; index < _reverseConvervsionInfoList->Count(); index++)
	{
		LanguageProfileInfo *infoItem = nullptr;
		infoItem = _reverseConvervsionInfoList->GetAt(index);
		delete[] infoItem->description;
	}
	_reverseConvervsionInfoList->Clear();
}

void CConfig::EnumerateReverseConversionProviders(LANGID langid)
{
	CTfInputProcessorProfile* profile = new (std::nothrow) CTfInputProcessorProfile();
	if (profile && SUCCEEDED(profile->CreateInstance()))
	{
		if (langid == 0)
			profile->GetCurrentLanguage(&langid);
		CDIMEArray<LanguageProfileInfo> langProfileInfoList;
		profile->GetReverseConversionProviders(langid, &langProfileInfoList);
		SetReverseConvervsionInfoList(&langProfileInfoList);
	}
	if (profile) delete profile;
}

void CConfig::SetReverseConversionDescriptionText(_In_opt_ const WCHAR* text)
{
	RustConfigValues(); // make sure the storage is initialised
	dime_settings_set_description(dime_settings_global(), reinterpret_cast<const uint16_t*>(text),
		text ? wcslen(text) : 0, text ? 1 : 0);
}

void CConfig::SetReverseConversionSelection(UINT sel)
{
	if (sel == 0)
	{
		_reverseConverstionCLSID = CLSID_NULL;
		_reverseConversionGUIDProfile = CLSID_NULL;
		SetReverseConversionDescriptionText(L"(無)");
	}
	else
	{
		sel--;
		_reverseConverstionCLSID = _reverseConvervsionInfoList->GetAt(sel)->clsid;
		_reverseConversionGUIDProfile = _reverseConvervsionInfoList->GetAt(sel)->guidProfile;
		SetReverseConversionDescriptionText(_reverseConvervsionInfoList->GetAt(sel)->description);
	}
	// Completes REVERSE_CONV_REFACTOR.md item B: any selection change through
	// this API must mark the IME-side _pITfReverseConversion[] cache for reload.
	// The legacy ConfigDialog also did this on combo SELCHANGE; the new
	// SettingsWindow had no equivalent line, leaving the cache stale.
	_reloadReverseConversion = TRUE;
}


BOOL CConfig::parseCINFile(_In_ LPCWSTR pathToLoad, _In_ LPCWSTR pathToWrite, _In_ BOOL customTableMode, _In_ BOOL suppressUI)
{
	FILE *fpr, *fpw;
	errno_t ret;
	BOOL success = TRUE;
	ret = _wfopen_s(&fpr, pathToLoad, (customTableMode) ? L"r, ccs=UTF-16LE" : L"r, ccs=UTF-8"); // custom table custom.txt in roaming profile is in UTF-16LE encoding.
	if (ret != 0)
	{
		if (!suppressUI)
			MessageBox(GetFocus(), L"指定檔案無法開啟!!", L"File open error!", MB_ICONERROR);
		return FALSE;
	}
	else
	{
		if (_wfopen_s(&fpw, pathToWrite, L"w+, ccs=UTF-16LE") == 0)
		{
			WCHAR line[MAX_COMMIT_LENGTH] = { 0 }, key[MAX_KEY_LENGTH] = { 0 }, value[MAX_VALUE_LENGTH] = { 0 }, escapedKey[MAX_KEY_LENGTH] = { 0 },
				escapedValue[MAX_VALUE_LENGTH] = { 0 },	sep[MAX_KEY_LENGTH] = { 0 }, others[MAX_COMMIT_LENGTH] = { 0 };
			BOOL doEscape = FALSE;

			if (customTableMode) {
				fwprintf_s(fpw, L"%s", L"%chardef begin\n");
				doEscape = TRUE;
			}

			while (fgetws(line, MAX_TABLE_LINE_LENGTH, fpr) != NULL)
			{
				// Try parsing key-value pair with 3 format patterns:
				// 1) key (with spaces, delimited by tab/newline) + whitespace sep + value (no spaces)
				// 2) key (no spaces) + whitespace sep + value (no spaces)
				// 3) key (no spaces) + whitespace sep + value (with spaces)
				if (swscanf_s(line, L"%[^\t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3
					&& swscanf_s(line, L"%[^ \t\r\n]%[ \t\r\n]%[^ \t\r\n]%s", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value), others, (int)_countof(others)) != 3
					&& swscanf_s(line, L"%[^ \t\r\n]%[ \t\r\n]%[^\t\r\n]", key, (int)_countof(key), sep, (int)_countof(sep), value, (int)_countof(value)) != 3)
				{
					// All 3 patterns failed to parse a key-value pair, so treat the line as unparsable.
					// Write unparsable lines as-is only outside of escape sections;
					// inside escape sections, skip them to avoid corrupting escaped output.
					if (!doEscape)
						fwprintf_s(fpw, L"%s", line);
					continue;
				}
				if ((CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%keyname", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"begin", 5) == CSTR_EQUAL) ||
					(CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%chardef", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"begin", 5) == CSTR_EQUAL))
				{
					doEscape = TRUE;
					fwprintf_s(fpw, L"%s %s\n", key, value);
					continue;
				}
				else if ((CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%keyname", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"end", 3) == CSTR_EQUAL) ||
					(CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%chardef", 8) == CSTR_EQUAL
					&& CompareString(1028, NORM_IGNORECASE, value, (int)wcslen(value), L"end", 3) == CSTR_EQUAL))
				{
					doEscape = FALSE;
					fwprintf_s(fpw, L"%s %s\n", key, value);
					continue;
				}
				else if (CompareString(1028, NORM_IGNORECASE, key, (int)wcslen(key), L"%encoding", 9) == CSTR_EQUAL)
				{
					fwprintf_s(fpw, L"%s\tUTF-16LE\n", key);
					continue;
				}

				if (doEscape)
				{
					// Check if key and value are already quoted (start and end with ")
					size_t keyLen = wcslen(key);
					size_t valLen = wcslen(value);
					BOOL keyAlreadyQuoted = (keyLen >= 2 && key[0] == L'"' && key[keyLen - 1] == L'"');
					BOOL valAlreadyQuoted = (valLen >= 2 && value[0] == L'"' && value[valLen - 1] == L'"');

					if (keyAlreadyQuoted && valAlreadyQuoted)
					{
						// Already in escaped format — write as-is to avoid double-escaping
						fwprintf_s(fpw, L"%s\t%s\n", key, value);
					}
					else
					{
						StringCchCopy(escapedKey, _countof(escapedKey), L"\"");
						for (UINT i = 0; i < wcslen(key); i++)
						{
							if (key[i] == '"') //escape " .
							{
								StringCchCat(escapedKey, _countof(escapedKey), L"\\\"");
							}
							else if (key[i] == '\\') // escape \ .
							{
								StringCchCat(escapedKey, _countof(escapedKey), L"\\\\");
							}
							else
							{
								StringCchCatN(escapedKey, _countof(escapedKey), &key[i], 1);
							}
						}
						StringCchCat(escapedKey, _countof(escapedKey), L"\"");
						StringCchCopy(escapedValue, _countof(escapedValue), L"\"");
						for (UINT i = 0; i < wcslen(value); i++)
						{
							if (value[i] == '"') //escape " .
							{
								StringCchCat(escapedValue, _countof(escapedValue), L"\\\"");
							}
							else if (value[i] == '\\') // escape \ .
							{
								StringCchCat(escapedValue, _countof(escapedValue), L"\\\\");
							}
							else
							{
								StringCchCatN(escapedValue, _countof(escapedValue), &value[i], 1);
							}
						}
						StringCchCat(escapedValue, _countof(escapedValue), L"\"");
						fwprintf_s(fpw, L"%s\t%s\n", escapedKey, escapedValue);
					}
				}
				else
					fwprintf_s(fpw, L"%s\t%s\n", key, value);

		}
		if (customTableMode) fwprintf_s(fpw, L"%s", L"%chardef end\n");
		fclose(fpw);


	}
		else
		{
			success = FALSE;
		}
}
fclose(fpr);
return success;
}

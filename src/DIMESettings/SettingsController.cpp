// SettingsModel.cpp — Implementation of the pure data layer for modernized settings UI
//
// All functions here are pure (no HWND, no GDI) and fully unit-testable.

#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <strsafe.h>
#include <shlobj.h>
#include <sys/stat.h>
#include "..\define.h"
#include "..\BaseStructure.h"
#include "..\Globals.h"
#include "..\Config.h"
#include "..\resource.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsController.h"
#include "SettingsPageLayout.h"
#include "dime_ffi.h"
#include <cstddef>

// SettingsSnapshot must match dime-config::settings_model::SettingsSnapshot.
static_assert(sizeof(SettingsSnapshot) == 216, "SettingsSnapshot size");
static_assert(alignof(SettingsSnapshot) == 4, "SettingsSnapshot alignment");
static_assert(offsetof(SettingsSnapshot, fontFaceName) == 12, "fontFaceName");
static_assert(offsetof(SettingsSnapshot, colorMode) == 76, "colorMode");
static_assert(offsetof(SettingsSnapshot, autoCompose) == 104, "autoCompose");
static_assert(offsetof(SettingsSnapshot, showNotifyDesktop) == 144, "showNotifyDesktop");
static_assert(offsetof(SettingsSnapshot, big5Filter) == 176, "big5Filter");
static_assert(offsetof(SettingsSnapshot, maxCodes) == 180, "maxCodes");
static_assert(offsetof(SettingsSnapshot, reverseConversionIndex) == 212, "reverseConversionIndex");
static_assert(sizeof(SettingsModel::LineError) == sizeof(int), "LineError");
static_assert(static_cast<int>(SettingsModel::LineError::None) == 0, "LineError::None");
static_assert(static_cast<int>(SettingsModel::LineError::Format) == 1, "LineError::Format");
static_assert(static_cast<int>(SettingsModel::LineError::KeyTooLong) == 2, "LineError::KeyTooLong");
static_assert(static_cast<int>(SettingsModel::LineError::InvalidChar) == 3, "LineError::InvalidChar");
static_assert(static_cast<int>(IME_MODE::IME_MODE_NONE) == 4, "IME_MODE_NONE");
static_assert(CTRL_SHOW_NOTIFY == 11, "CTRL_SHOW_NOTIFY");
static_assert(CTRL_CHARSET_SCOPE == 14, "CTRL_CHARSET_SCOPE");
static_assert(CTRL_NUMERIC_PAD == 15, "CTRL_NUMERIC_PAD");
static_assert(CTRL_SHIFT_ENGLISH_DIGIT_MODE == 16, "CTRL_SHIFT_ENGLISH_DIGIT_MODE");
static_assert(CTRL_PHONETIC_KB == 17, "CTRL_PHONETIC_KB");
static_assert(CTRL_ARROW_KEY_SW_PAGES == 19, "CTRL_ARROW_KEY_SW_PAGES");
static_assert(CTRL_SPACE_AS_PAGEDOWN == 20, "CTRL_SPACE_AS_PAGEDOWN");
static_assert(CTRL_SPACE_AS_FIRST_CAND == 21, "CTRL_SPACE_AS_FIRST_CAND");
static_assert(CTRL_AUTO_COMPOSE == 22, "CTRL_AUTO_COMPOSE");
static_assert(CTRL_CLEAR_ON_BEEP == 23, "CTRL_CLEAR_ON_BEEP");
static_assert(CTRL_MAKE_PHRASE == 24, "CTRL_MAKE_PHRASE");
static_assert(CTRL_CUSTOM_TABLE_PRIORITY == 25, "CTRL_CUSTOM_TABLE_PRIORITY");
static_assert(CTRL_DO_BEEP == 26, "CTRL_DO_BEEP");
static_assert(CTRL_DO_BEEP_NOTIFY == 27, "CTRL_DO_BEEP_NOTIFY");
static_assert(CTRL_DO_BEEP_CANDI == 28, "CTRL_DO_BEEP_CANDI");
static_assert(CTRL_IME_SHIFT_MODE == 29, "CTRL_IME_SHIFT_MODE");
static_assert(CTRL_DOUBLE_SINGLE_BYTE == 30, "CTRL_DOUBLE_SINGLE_BYTE");
static_assert(CTRL_KEYBOARD_OPEN_CLOSE == 31, "CTRL_KEYBOARD_OPEN_CLOSE");
static_assert(CTRL_OUTPUT_CHT_CHS == 32, "CTRL_OUTPUT_CHT_CHS");
static_assert(CTRL_DAYI_ARTICLE == 34, "CTRL_DAYI_ARTICLE");
static_assert(CTRL_SENTENCE_MODE == 35, "CTRL_SENTENCE_MODE");
static_assert(CTRL_DAYI_SELKEY_MODE == 36, "CTRL_DAYI_SELKEY_MODE");
static_assert(CTRL_ARRAY_FORCE_SP == 37, "CTRL_ARRAY_FORCE_SP");
static_assert(CTRL_ARRAY_NOTIFY_SP == 38, "CTRL_ARRAY_NOTIFY_SP");
static_assert(CTRL_ARRAY_SINGLE_QUOTE == 39, "CTRL_ARRAY_SINGLE_QUOTE");
static_assert(CTRL_LOAD_DAYI_MAIN == 300, "CTRL_LOAD_DAYI_MAIN");
static_assert(CTRL_LOAD_ARRAY_MAIN == 301, "CTRL_LOAD_ARRAY_MAIN");
static_assert(CTRL_LOAD_ARRAY_EXPAND == 302, "CTRL_LOAD_ARRAY_EXPAND");
static_assert(CTRL_LOAD_ARRAY_SP == 303, "CTRL_LOAD_ARRAY_SP");
static_assert(CTRL_LOAD_ARRAY_SC == 304, "CTRL_LOAD_ARRAY_SC");
static_assert(CTRL_LOAD_ARRAY_EXT_B == 305, "CTRL_LOAD_ARRAY_EXT_B");
static_assert(CTRL_LOAD_ARRAY_EXT_CD == 306, "CTRL_LOAD_ARRAY_EXT_CD");
static_assert(CTRL_LOAD_ARRAY_EXT_E_TO_J == 307, "CTRL_LOAD_ARRAY_EXT_E_TO_J");
static_assert(CTRL_LOAD_ARRAY40 == 308, "CTRL_LOAD_ARRAY40");
static_assert(CTRL_LOAD_ARRAY_PHRASE == 309, "CTRL_LOAD_ARRAY_PHRASE");
static_assert(CTRL_LOAD_PHONETIC_MAIN == 310, "CTRL_LOAD_PHONETIC_MAIN");
static_assert(CTRL_LOAD_GENERIC_MAIN == 311, "CTRL_LOAD_GENERIC_MAIN");
static_assert(CTRL_LOAD_ASSOC_PHRASE == 312, "CTRL_LOAD_ASSOC_PHRASE");

namespace {
extern "C" int32_t DimeSettingsCharOk(uint16_t ch, void* ctx)
{
    CCompositionProcessorEngine* engine = static_cast<CCompositionProcessorEngine*>(ctx);
    if (engine == nullptr)
    {
        return 0;
    }
    return engine->ValidateCompositionKeyCharFull(static_cast<WCHAR>(ch)) ? 1 : 0;
}
}

// ============================================================================
// IME mode bitmask
// ============================================================================

int SettingsModel::GetModeBitmask(IME_MODE mode)
{
    return dime_settings_mode_bitmask(static_cast<int32_t>(mode));
}

// ============================================================================
// Sidebar hitTest
// ============================================================================

int SettingsModel::SidebarHitTest(int y, int itemHeight, int itemCount)
{
    return dime_settings_sidebar_hit_test(y, itemHeight, itemCount);
}

// ============================================================================
// Scroll computation
// ============================================================================

int SettingsModel::ComputeScrollRange(int contentHeight, int viewportHeight)
{
    return dime_settings_scroll_range(contentHeight, viewportHeight);
}

int SettingsModel::ClampScrollPos(int pos, int maxScroll)
{
    return dime_settings_clamp_scroll(pos, maxScroll);
}

// ============================================================================
// IME mode string conversion
// ============================================================================

const wchar_t* SettingsModel::ImeModeToString(IME_MODE mode)
{
    return reinterpret_cast<const wchar_t*>(dime_settings_ime_mode_to_string(static_cast<int32_t>(mode)));
}

IME_MODE SettingsModel::StringToImeMode(const wchar_t* str)
{
    return static_cast<IME_MODE>(dime_settings_string_to_ime_mode(reinterpret_cast<const uint16_t*>(str)));
}

// ============================================================================
// Settings snapshot — CConfig bridge
// ============================================================================

SettingsSnapshot SettingsModel::LoadFromConfig()
{
#ifdef DIME_USE_RUST_CONFIG
    uint32_t colors[6] = {};
    const ColorInfo* clrs = CConfig::GetColors();
    if (clrs != nullptr)
    {
        for (int i = 0; i < 6; ++i)
        {
            colors[i] = clrs[i].color;
        }
    }
    SettingsSnapshot snap = {};
    dime_settings_load_snapshot(&snap, colors);
    return snap;
#else
    SettingsSnapshot snap = {};

    snap.fontSize      = CConfig::GetFontSize();
    snap.fontWeight     = CConfig::GetFontWeight();
    snap.fontItalic     = CConfig::GetFontItalic();
    StringCchCopyW(snap.fontFaceName, LF_FACESIZE, CConfig::GetFontFaceName());
    snap.colorMode      = (int)CConfig::GetColorMode();

    // Load effective palette colors (respects system dark/light mode).
    // Caller must call CConfig::LoadColorsForMode() before this if needed.
    {
        const ColorInfo* clrs = CConfig::GetColors();
        snap.itemColor       = clrs[0].color;
        snap.selectedColor   = clrs[1].color;
        snap.itemBGColor     = clrs[2].color;
        snap.phraseColor     = clrs[3].color;
        snap.numberColor     = clrs[4].color;
        snap.selectedBGColor = clrs[5].color;
    }

    snap.autoCompose             = CConfig::GetAutoCompose();
    snap.clearOnBeep             = CConfig::GetClearOnBeep();
    snap.doBeep                  = CConfig::GetDoBeep();
    snap.doBeepNotify            = CConfig::GetDoBeepNotify();
    snap.doBeepOnCandi           = CConfig::GetDoBeepOnCandi();
    snap.makePhrase              = CConfig::GetMakePhrase();
    snap.customTablePriority     = CConfig::getCustomTablePriority();
    snap.arrowKeySWPages         = CConfig::GetArrowKeySWPages();
    snap.spaceAsPageDown         = CConfig::GetSpaceAsPageDown();
    snap.spaceAsFirstCandSelkey  = CConfig::GetSpaceAsFirstCaniSelkey();
    snap.showNotifyDesktop       = CConfig::GetShowNotifyDesktop();
    snap.arrayNotifySP           = CConfig::GetArrayNotifySP();
    snap.arrayForceSP            = CConfig::GetArrayForceSP();
    snap.arraySingleQuoteCustomPhrase = CConfig::GetArraySingleQuoteCustomPhrase();
    snap.dayiArticleMode         = CConfig::getDayiArticleMode();
    snap.sentenceMode            = CConfig::GetSentenceMode();
    snap.doHanConvert            = CConfig::GetDoHanConvert();
    snap.activatedKeyboardMode   = CConfig::GetActivatedKeyboardMode();
    snap.big5Filter              = CConfig::GetBig5Filter();
    snap.maxCodes                = CConfig::GetMaxCodes();
    snap.arrayScope              = (int)CConfig::GetArrayScope();
    snap.numericPad              = (int)CConfig::GetNumericPad();
    snap.phoneticKeyboardLayout  = (int)CConfig::getPhoneticKeyboardLayout();
    snap.dayiSelkeyMode          = (int)CConfig::GetDayiSelkeyMode();
    snap.imeShiftMode            = (int)CConfig::GetIMEShiftMode();
    snap.doubleSingleByteMode    = (int)CConfig::GetDoubleSingleByteMode();
    snap.shiftEnglishDigitMode   = (int)CConfig::GetShiftEnglishDigitMode();

    snap.reverseConversionIndex  = 0; // default; UI populates from reverse conversion list

    return snap;
#endif
}

void SettingsModel::ApplyToConfig(const SettingsSnapshot& snap)
{
#ifdef DIME_USE_RUST_CONFIG
    dime_settings_apply_snapshot(&snap);
#else
    CConfig::SetFontSize(snap.fontSize);
    CConfig::SetFontWeight(snap.fontWeight);
    CConfig::SetFontItalic(snap.fontItalic);
    CConfig::SetFontFaceName(const_cast<WCHAR*>(snap.fontFaceName));
    CConfig::SetColorMode((IME_COLOR_MODE)snap.colorMode);

    // Do NOT write palette colors here — snapshot holds effective colors
    // (may be light/dark/system palette), not custom palette values.
    // Custom palette is only written via SaveColorsForMode() when the user
    // explicitly changes a color swatch in CUSTOM mode.

    CConfig::SetAutoCompose(snap.autoCompose);
    CConfig::SetClearOnBeep(snap.clearOnBeep);
    CConfig::SetDoBeep(snap.doBeep);
    CConfig::SetDoBeepNotify(snap.doBeepNotify);
    CConfig::SetDoBeepOnCandi(snap.doBeepOnCandi);
    CConfig::SetMakePhrase(snap.makePhrase);
    CConfig::setCustomTablePriority(snap.customTablePriority);
    CConfig::SetArrowKeySWPages(snap.arrowKeySWPages);
    CConfig::SetSpaceAsPageDown(snap.spaceAsPageDown);
    CConfig::SetSpaceAsFirstCaniSelkey(snap.spaceAsFirstCandSelkey);
    CConfig::SetShowNotifyDesktop(snap.showNotifyDesktop);
    CConfig::SetArrayNotifySP(snap.arrayNotifySP);
    CConfig::SetArrayForceSP(snap.arrayForceSP);
    CConfig::SetArraySingleQuoteCustomPhrase(snap.arraySingleQuoteCustomPhrase);
    CConfig::setDayiArticleMode(snap.dayiArticleMode);
    CConfig::SetSentenceMode(snap.sentenceMode);
    CConfig::SetDoHanConvert(snap.doHanConvert);
    CConfig::SetActivatedKeyboardMode(snap.activatedKeyboardMode);
    CConfig::SetBig5Filter(snap.big5Filter);
    CConfig::SetMaxCodes(snap.maxCodes);
    CConfig::SetArrayScope((ARRAY_SCOPE)snap.arrayScope);
    CConfig::SetNumericPad((NUMERIC_PAD)snap.numericPad);
    CConfig::setPhoneticKeyboardLayout((PHONETIC_KEYBOARD_LAYOUT)snap.phoneticKeyboardLayout);
    CConfig::SetDayiSelkeyMode((DAYI_SELKEY_MODE)snap.dayiSelkeyMode);
    CConfig::SetIMEShiftMode((IME_SHIFT_MODE)snap.imeShiftMode);
    CConfig::SetDoubleSingleByteMode((DOUBLE_SINGLE_BYTE_MODE)snap.doubleSingleByteMode);
    CConfig::SetShiftEnglishDigitMode((IME_SHIFT_ENGLISH_DIGIT_MODE)snap.shiftEnglishDigitMode);
#endif
}

void SettingsModel::SyncToggleToSnapshot(SettingsSnapshot& s, SettingsControlId id, bool isOn)
{
    dime_settings_sync_toggle(&s, static_cast<int32_t>(id), isOn ? 1 : 0);
}

SettingsModel::LineValidation SettingsModel::ValidateLine(const wchar_t* line, int len, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEngine)
{
    DimeLineError errs[1024];
    size_t count = 0;
    const int32_t ok = dime_settings_validate_line(
        reinterpret_cast<const uint16_t*>(line),
        (line != nullptr && len > 0) ? static_cast<size_t>(len) : 0,
        static_cast<int32_t>(mode),
        maxCodes,
        (pEngine != nullptr) ? &DimeSettingsCharOk : nullptr,
        pEngine,
        errs,
        1024,
        &count);
    LineValidation v;
    v.valid = (ok != 0);
    if (count > 1024) count = 1024;
    v.errors.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        LineError kind = LineError::None;
        if (errs[i].code == static_cast<int32_t>(LineError::Format)) kind = LineError::Format;
        else if (errs[i].code == static_cast<int32_t>(LineError::KeyTooLong)) kind = LineError::KeyTooLong;
        else if (errs[i].code == static_cast<int32_t>(LineError::InvalidChar)) kind = LineError::InvalidChar;
        v.errors.push_back({ kind, errs[i].start, errs[i].len });
    }
    return v;
}

// ============================================================================
// Composition engine lifecycle
// ============================================================================

CCompositionProcessorEngine* SettingsModel::CreateEngine(IME_MODE mode)
{
    auto* pEngine = new (std::nothrow) CCompositionProcessorEngine(nullptr);
    if (pEngine) {
        pEngine->SetupDictionaryFile(mode);
        pEngine->SetupConfiguration(mode);
        pEngine->SetupKeystroke(mode);
    }
    return pEngine;
}

void SettingsModel::DestroyEngine(CCompositionProcessorEngine*& pEngine)
{
    if (pEngine) {
        delete pEngine;
        pEngine = nullptr;
    }
}

// ============================================================================
// Custom table file paths
// ============================================================================

void SettingsModel::GetCustomTableTxtPath(IME_MODE mode, WCHAR* out, DWORD cch)
{
    WCHAR appData[MAX_PATH] = {};
    Global::GetAppDataPath(appData, TRUE);
    const wchar_t* suffix;
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     suffix = L"\\DIME\\DAYI-Custom.txt";     break;
    case IME_MODE::IME_MODE_ARRAY:    suffix = L"\\DIME\\ARRAY-Custom.txt";    break;
    case IME_MODE::IME_MODE_PHONETIC: suffix = L"\\DIME\\PHONETIC-Custom.txt"; break;
    default:                          suffix = L"\\DIME\\GENERIC-Custom.txt";  break;
    }
    StringCchPrintfW(out, cch, L"%s%s", appData, suffix);
}

void SettingsModel::GetCustomTableCINPath(IME_MODE mode, WCHAR* out, DWORD cch)
{
    WCHAR appData[MAX_PATH] = {};
    Global::GetAppDataPath(appData, TRUE);
    const wchar_t* suffix;
    switch (mode) {
    case IME_MODE::IME_MODE_DAYI:     suffix = L"\\DIME\\DAYI-Custom.cin";     break;
    case IME_MODE::IME_MODE_ARRAY:    suffix = L"\\DIME\\ARRAY-Custom.cin";    break;
    case IME_MODE::IME_MODE_PHONETIC: suffix = L"\\DIME\\PHONETIC-Custom.cin"; break;
    default:                          suffix = L"\\DIME\\GENERIC-Custom.cin";  break;
    }
    StringCchPrintfW(out, cch, L"%s%s", appData, suffix);
}

// ============================================================================
// Encoding-aware text loader (issue #130)
// ============================================================================
// The legacy ImportCustomTable / initial-load path read raw bytes straight
// into a WCHAR[] buffer with no encoding conversion. For ANSI/Big5/UTF-8
// sources the bytes reinterpreted to wide chars with zero whitespace, which
// in turn fed the format-checker regex catastrophic input. This helper
// detects encoding and returns a real UTF-16LE wide string.
LPWSTR SettingsModel::LoadTextFileAsUtf16(LPCWSTR path, size_t* outLen)
{
    // File open and the empty-file buffer stay here. The byte ladder,
    // including the UTF-16 BE double swap, is dime_settings_decode_text*.
    if (outLen) *outLen = 0;
    if (!path) return nullptr;

    HANDLE hFile = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD dwSize = GetFileSize(hFile, nullptr);
    if (dwSize == INVALID_FILE_SIZE || dwSize == 0) {
        CloseHandle(hFile);
        LPWSTR empty = new (std::nothrow) WCHAR[1];
        if (empty) empty[0] = 0;
        return empty;
    }

    BYTE* raw = new (std::nothrow) BYTE[dwSize];
    if (!raw) { CloseHandle(hFile); return nullptr; }
    DWORD dwRead = 0;
    if (!ReadFile(hFile, raw, dwSize, &dwRead, nullptr) || dwRead == 0) {
        delete[] raw;
        CloseHandle(hFile);
        return nullptr;
    }
    CloseHandle(hFile);

    uint32_t wlen = 0;
    if (dime_settings_decode_text_len(raw, dwRead, &wlen) == 0) {
        delete[] raw;
        return nullptr;
    }
    LPWSTR out = new (std::nothrow) WCHAR[static_cast<size_t>(wlen) + 1];
    if (!out) { delete[] raw; return nullptr; }
    if (dime_settings_decode_text(raw, dwRead, reinterpret_cast<uint16_t*>(out), wlen) == 0) {
        delete[] out;
        delete[] raw;
        return nullptr;
    }
    out[wlen] = 0;
    delete[] raw;
    if (outLen) *outLen = wlen;
    return out;
}

// ============================================================================
// Sidebar index ↔ IME mode
// ============================================================================

IME_MODE SettingsModel::IndexToMode(int index)
{
    return static_cast<IME_MODE>(dime_settings_index_to_mode(index));
}

int SettingsModel::ModeToIndex(IME_MODE mode)
{
    return dime_settings_mode_to_index(static_cast<int32_t>(mode));
}

// ============================================================================
// Combo selection → snapshot field mapping
// ============================================================================

void SettingsModel::SyncComboToSnapshot(SettingsSnapshot& s, SettingsControlId id, int sel)
{
    dime_settings_sync_combo(&s, static_cast<int32_t>(id), sel);
}

// ============================================================================
// CTRL_MAX_CODES text → value
// ============================================================================

bool SettingsModel::ParseMaxCodes(const wchar_t* text, UINT& outVal)
{
    uint32_t val = 0;
    if (!dime_settings_parse_max_codes(reinterpret_cast<const uint16_t*>(text), &val))
    {
        return false;
    }
    outVal = val;
    return true;
}

// ============================================================================
// Load table control ID → CIN file suffix
// ============================================================================

const wchar_t* SettingsModel::GetLoadTableSuffix(SettingsControlId id)
{
    return reinterpret_cast<const wchar_t*>(dime_settings_load_table_suffix(static_cast<int32_t>(id)));
}

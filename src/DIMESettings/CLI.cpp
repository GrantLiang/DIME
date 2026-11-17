// CLI.cpp — Headless CLI implementation for DIMESettings.exe
//
// See CLI.h for public API documentation.

#include "framework.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <objbase.h>    // StringFromCLSID, CLSIDFromString
#include "CLI.h"
#include "..\define.h"
#include "..\Config.h"
#include "..\Globals.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsController.h"
#include "dime_ffi.h"

// ---------------------------------------------------------------------------
// Key registry
// ---------------------------------------------------------------------------

static const KeyInfo g_keyRegistry[] =
{
    // Boolean keys
    { L"AutoCompose",                  BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"SpaceAsPageDown",              BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"SpaceAsFirstCandSelkey",       BOOL_T,   0,   1,   CLI_MASK_GENERIC   },
    { L"ArrowKeySWPages",              BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"ClearOnBeep",                  BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoBeep",                       BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoBeepNotify",                 BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoBeepOnCandi",                BOOL_T,   0,   1,   CLI_MASK_DAYI      },
    { L"ActivatedKeyboardMode",        BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"MakePhrase",                   BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"ShowNotifyDesktop",            BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DoHanConvert",                 BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"FontItalic",                   BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"CustomTablePriority",          BOOL_T,   0,   1,   CLI_MASK_ALL       },
    { L"DayiArticleMode",              BOOL_T,   0,   1,   CLI_MASK_DAYI      },
    { L"SentenceMode",                 BOOL_T,   0,   1,   CLI_MASK_DAYI      },
    { L"ArrayForceSP",                 BOOL_T,   0,   1,   CLI_MASK_ARRAY     },
    { L"ArrayNotifySP",                BOOL_T,   0,   1,   CLI_MASK_ARRAY     },
    { L"ArraySingleQuoteCustomPhrase", BOOL_T,   0,   1,   CLI_MASK_ARRAY     },
    { L"Big5Filter",                   BOOL_T,   0,   1,   CLI_MASK_NOT_ARRAY },
    // Integer / enum keys
    { L"MaxCodes",                     INT_T,    1,  20,   CLI_MASK_ALL       },
    { L"FontSize",                     INT_T,    8,  72,   CLI_MASK_ALL       },
    { L"FontWeight",                   INT_T,  100, 900,   CLI_MASK_ALL       },
    { L"IMEShiftMode",                 INT_T,    0,   3,   CLI_MASK_ALL       },
    { L"DoubleSingleByteMode",         INT_T,    0,   2,   CLI_MASK_ALL       },
    { L"ShiftEnglishDigitMode",        INT_T,    0,   2,   CLI_MASK_ALL       },
    { L"ColorMode",                    INT_T,    0,   3,   CLI_MASK_ALL       },
    { L"NumericPad",                   INT_T,    0,   2,   CLI_MASK_ALL       },
    { L"ArrayScope",                   INT_T,    0,   5,   CLI_MASK_ARRAY     },
    { L"PhoneticKeyboardLayout",       INT_T,    0,   1,   CLI_MASK_PHONETIC  },
    { L"DayiSelkeyMode",               INT_T,    0,   2,   CLI_MASK_DAYI      },
    // String keys
    { L"FontFaceName",                 STRING_T, 0,   0,   CLI_MASK_ALL       },
    { L"ReverseConversionDescription", STRING_T, 0,   0,   CLI_MASK_ALL       },
    { L"ReverseConversionCLSID",       CLSID_T,  0,   0,   CLI_MASK_ALL       },
    { L"ReverseConversionGUIDProfile", CLSID_T,  0,   0,   CLI_MASK_ALL       },
    // Custom-palette color keys (ColorMode=3)
    { L"ItemColor",                    COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"PhraseColor",                  COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"NumberColor",                  COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"ItemBGColor",                  COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"SelectedItemColor",            COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"SelectedBGItemColor",          COLOR_T,  0,   0,   CLI_MASK_ALL       },
    // Light-palette color keys (ColorMode=1)
    { L"LightItemColor",               COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightPhraseColor",             COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightNumberColor",             COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightItemBGColor",             COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightSelectedItemColor",       COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"LightSelectedBGItemColor",     COLOR_T,  0,   0,   CLI_MASK_ALL       },
    // Dark-palette color keys (ColorMode=2)
    { L"DarkItemColor",                COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkPhraseColor",              COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkNumberColor",              COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkItemBGColor",              COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkSelectedItemColor",        COLOR_T,  0,   0,   CLI_MASK_ALL       },
    { L"DarkSelectedBGItemColor",      COLOR_T,  0,   0,   CLI_MASK_ALL       },
};

static const int g_keyCount = (int)(sizeof(g_keyRegistry) / sizeof(g_keyRegistry[0]));

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Find a key entry by name (case-insensitive).
static const KeyInfo* FindKey(const wchar_t* name)
{
    for (int i = 0; i < g_keyCount; i++)
        if (_wcsicmp(g_keyRegistry[i].name, name) == 0)
            return &g_keyRegistry[i];
    return nullptr;
}

// Returns true when the key is applicable to the given IME mode.
static bool KeyApplicableToMode(const KeyInfo* ki, IME_MODE mode)
{
    int bit = 0;
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     bit = CLI_MASK_DAYI;     break;
    case IME_MODE::IME_MODE_ARRAY:    bit = CLI_MASK_ARRAY;    break;
    case IME_MODE::IME_MODE_PHONETIC: bit = CLI_MASK_PHONETIC; break;
    case IME_MODE::IME_MODE_GENERIC:  bit = CLI_MASK_GENERIC;  break;
    default: return false;
    }
    return (ki->modeMask & bit) != 0;
}

// Returns a short string identifier for the mode (for error messages).
static const wchar_t* ModeName(IME_MODE mode)
{
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     return L"dayi";
    case IME_MODE::IME_MODE_ARRAY:    return L"array";
    case IME_MODE::IME_MODE_PHONETIC: return L"phonetic";
    case IME_MODE::IME_MODE_GENERIC:  return L"generic";
    default:                          return L"(none)";
    }
}

// Build %APPDATA%\DIME path into buf.
static void GetDimePath(WCHAR* buf, int bufLen)
{
    WCHAR appData[MAX_PATH] = {};
    Global::GetAppDataPath(appData, FALSE);
    StringCchPrintfW(buf, bufLen, L"%s\\DIME", appData);
}

// Build the path for the mode-specific custom table file.
static void GetCustomTablePath(IME_MODE mode, WCHAR* buf, int bufLen)
{
    WCHAR dimePath[MAX_PATH];
    GetDimePath(dimePath, MAX_PATH);
    const wchar_t* prefix = L"GENERIC";
    switch (mode)
    {
    case IME_MODE::IME_MODE_DAYI:     prefix = L"DAYI";     break;
    case IME_MODE::IME_MODE_ARRAY:    prefix = L"ARRAY";    break;
    case IME_MODE::IME_MODE_PHONETIC: prefix = L"PHONETIC"; break;
    default: break;
    }
    StringCchPrintfW(buf, bufLen, L"%s\\%s-CUSTOM.txt", dimePath, prefix);
}

// Parse a hex colour string ("0xRRGGBB" or "RRGGBB") into a COLORREF.
// Returns false if the string cannot be parsed.
static bool ParseColorValue(const wchar_t* str, COLORREF& out)
{
    if (!str || str[0] == L'\0') return false;

    const wchar_t* p = str;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))
        p += 2;

    wchar_t* end = nullptr;
    unsigned long val = wcstoul(p, &end, 16);
    if (end == p || *end != L'\0') return false;
    out = (COLORREF)val;
    return true;
}

// ---------------------------------------------------------------------------
// Rust implementation (rust/crates/dime-config, keys.rs): value formatting
// and validation run on a snapshot of CConfig. g_keyRegistry above must stay
// identical to the Rust registry (checked by RustCliParityTest).
// ---------------------------------------------------------------------------
static int RustKeyIndex(const KeyInfo* ki)
{
    return dime_key_find(reinterpret_cast<const uint16_t*>(ki->name), wcslen(ki->name));
}

static bool GetKeyValue(const KeyInfo* ki, WCHAR* outBuf, int bufLen)
{
    int index = RustKeyIndex(ki);
    if (index < 0) return false;
    DimeBuffer* text = nullptr;
    {
        CRustConfigScope settings;
        if (!settings.Get()) return false;
        text = dime_settings_get_key(settings.Get(), index);
    }
    if (!text) return false;
    size_t len = 0;
    const uint16_t* data = dime_buffer_data(text, &len);
    outBuf[0] = L'\0';
    if (len) StringCchCopyNW(outBuf, bufLen, reinterpret_cast<const wchar_t*>(data), len);
    dime_buffer_free(text);
    return true;
}

static int ApplyKeyValue(const KeyInfo* ki, const wchar_t* valueStr, FILE* err)
{
    int index = RustKeyIndex(ki);
    if (index < 0) return 1;
    DimeBuffer* message = nullptr;
    int rc;
    {
        CRustConfigScope settings;
        if (!settings.Get()) return 2;
        rc = dime_settings_set_key(settings.Get(), index, reinterpret_cast<const uint16_t*>(valueStr), wcslen(valueStr), &message);
        if (rc == 0) settings.Commit(); // a rejected value leaves the settings unchanged
    }
    if (message)
    {
        size_t len = 0;
        const uint16_t* data = dime_buffer_data(message, &len);
        fwprintf_s(err, L"%.*s\n", (int)len, reinterpret_cast<const wchar_t*>(data));
        dime_buffer_free(message);
    }
    return rc;
}

// Rust CLI (phase 5a). C++ keeps the handlers above for DimeUseRustEngine=false.
// Vtable order matches DimeCliHostVtbl in dime_ffi.h and dime-ffi/src/cli.rs.

static_assert(sizeof(DimeCliArgs) == 21160, "DimeCliArgs size");
static_assert(offsetof(DimeCliArgs, set_count) == 20632, "DimeCliArgs set_count");
static_assert(offsetof(DimeCliArgs, file_path) == 20636, "DimeCliArgs file_path");
static_assert(MAX_PATH == 260, "MAX_PATH");

struct CliRt
{
    FILE* out;
    FILE* err;
    CCompositionProcessorEngine* engine;
};

static CliRt* CliRtOf(DimeCliHost* h)
{
    return h ? static_cast<CliRt*>(h->ctx) : nullptr;
}

static void WriteWide(FILE* f, const uint16_t* s, size_t len)
{
    if (!f || !s || !len) return;
    size_t off = 0;
    while (off < len)
    {
        size_t chunk = len - off;
        if (chunk > 1048576) chunk = 1048576;
        fwprintf_s(f, L"%.*s", (int)chunk, reinterpret_cast<const wchar_t*>(s + off));
        off += chunk;
    }
}

static void ToZ(WCHAR* z, const uint16_t* p, size_t len)
{
    size_t n = len;
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    if (n && p) memcpy(z, p, n * sizeof(WCHAR));
    z[n] = 0;
}

static size_t CopyOut(uint16_t* out, size_t cap, const WCHAR* s)
{
    size_t n = s ? wcslen(s) : 0;
    if (!out || cap == 0) return n;
    size_t c = n < cap ? n : cap - 1;
    if (c) memcpy(out, s, c * sizeof(uint16_t));
    out[c] = 0;
    return c;
}

extern "C" {

static void CliWriteOut(DimeCliHost* h, const uint16_t* text, size_t len)
{
    CliRt* rt = CliRtOf(h);
    if (rt) WriteWide(rt->out, text, len);
}

static void CliWriteErr(DimeCliHost* h, const uint16_t* text, size_t len)
{
    CliRt* rt = CliRtOf(h);
    if (rt) WriteWide(rt->err, text, len);
}

static void CliLoadConfig(DimeCliHost*, int32_t mode)
{
    CConfig::LoadConfig((IME_MODE)mode);
}

static void CliWriteConfig(DimeCliHost*, int32_t mode)
{
    CConfig::WriteConfig((IME_MODE)mode, FALSE);
}

static void CliReset(DimeCliHost*, int32_t mode)
{
    CConfig::ResetAllDefaults((IME_MODE)mode);
}

static void CliGetValue(DimeCliHost*, int32_t index, DimeBuffer** out)
{
    if (out) *out = nullptr;
    CRustConfigScope settings;
    if (!settings.Get() || !out) return;
    *out = dime_settings_get_key(settings.Get(), index);
}

static int32_t CliSetValue(DimeCliHost*, int32_t index, const uint16_t* value, size_t len, DimeBuffer** error)
{
    if (error) *error = nullptr;
    DimeBuffer* message = nullptr;
    int rc;
    {
        CRustConfigScope settings;
        if (!settings.Get()) return 2;
        rc = dime_settings_set_key(settings.Get(), index, value, len, &message);
        if (rc == 0) settings.Commit();
    }
    if (error) *error = message;
    else if (message) dime_buffer_free(message);
    return (int32_t)rc;
}

static int32_t CliFileExists(DimeCliHost*, const uint16_t* path, size_t len)
{
    WCHAR z[MAX_PATH];
    ToZ(z, path, len);
    return PathFileExistsW(z) ? 1 : 0;
}

static int32_t CliLoadText(DimeCliHost*, const uint16_t* path, size_t len, uint16_t** out, size_t* outLen)
{
    if (out) *out = nullptr;
    if (outLen) *outLen = 0;
    WCHAR z[MAX_PATH];
    ToZ(z, path, len);
    size_t wlen = 0;
    LPWSTR text = SettingsModel::LoadTextFileAsUtf16(z, &wlen);
    if (!text) return 0;
    if (out) *out = reinterpret_cast<uint16_t*>(text);
    if (outLen) *outLen = wlen;
    return 1;
}

static void CliFreeText(DimeCliHost*, uint16_t* p)
{
    delete[] reinterpret_cast<WCHAR*>(p);
}

static uint32_t CliBeginValidate(DimeCliHost* h, int32_t mode)
{
    CliRt* rt = CliRtOf(h);
    if (!rt) return 0;
    if (rt->engine) SettingsModel::DestroyEngine(rt->engine);
    rt->engine = SettingsModel::CreateEngine((IME_MODE)mode);
    CConfig::LoadConfig((IME_MODE)mode);
    return (uint32_t)CConfig::GetMaxCodes();
}

static int32_t CliValidateLine(DimeCliHost* h, const uint16_t* line, size_t len, int32_t mode, uint32_t maxCodes)
{
    CliRt* rt = CliRtOf(h);
    int n = len > 0x7fffffff ? 0x7fffffff : (int)len;
    auto v = SettingsModel::ValidateLine(reinterpret_cast<const wchar_t*>(line), n,
        (IME_MODE)mode, maxCodes, rt ? rt->engine : nullptr);
    if (v.valid) return 0;
    if (v.errors.empty()) return 1;
    switch (v.errors[0].error)
    {
    case SettingsModel::LineError::KeyTooLong: return 2;
    case SettingsModel::LineError::InvalidChar: return 3;
    default: return 1;
    }
}

static void CliEndValidate(DimeCliHost* h)
{
    CliRt* rt = CliRtOf(h);
    if (rt && rt->engine) SettingsModel::DestroyEngine(rt->engine);
}

static size_t CliCustomTxt(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap)
{
    WCHAR buf[MAX_PATH];
    SettingsModel::GetCustomTableTxtPath((IME_MODE)mode, buf, MAX_PATH);
    return CopyOut(out, cap, buf);
}

static size_t CliCustomCin(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap)
{
    WCHAR buf[MAX_PATH];
    SettingsModel::GetCustomTableCINPath((IME_MODE)mode, buf, MAX_PATH);
    return CopyOut(out, cap, buf);
}

static int32_t CliWriteUtf16(DimeCliHost*, const uint16_t* path, size_t pathLen,
    const uint16_t* text, size_t textLen, uint32_t* gle)
{
    WCHAR z[MAX_PATH];
    ToZ(z, path, pathLen);
    HANDLE hFile = CreateFileW(z, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        if (gle) *gle = GetLastError();
        return 1;
    }
    DWORD written = 0;
    WCHAR bom = 0xFEFF;
    const void* data = text ? static_cast<const void*>(text) : static_cast<const void*>(L"");
    BOOL ok = WriteFile(hFile, &bom, sizeof(WCHAR), &written, nullptr)
           && WriteFile(hFile, data, (DWORD)(textLen * sizeof(uint16_t)), &written, nullptr);
    CloseHandle(hFile);
    if (!ok) return 2;
    return 0;
}

static int32_t CliParseCin(DimeCliHost*, const uint16_t* src, size_t srcLen,
    const uint16_t* dst, size_t dstLen, int32_t custom)
{
    WCHAR a[MAX_PATH], b[MAX_PATH];
    ToZ(a, src, srcLen);
    ToZ(b, dst, dstLen);
    return CConfig::parseCINFile(a, b, custom ? TRUE : FALSE, TRUE) ? 1 : 0;
}

static size_t CliExportSrc(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap)
{
    WCHAR buf[MAX_PATH];
    GetCustomTablePath((IME_MODE)mode, buf, MAX_PATH);
    return CopyOut(out, cap, buf);
}

static int32_t CliCopyFile(DimeCliHost*, const uint16_t* src, size_t srcLen,
    const uint16_t* dst, size_t dstLen, uint32_t* gle)
{
    WCHAR a[MAX_PATH], b[MAX_PATH];
    ToZ(a, src, srcLen);
    ToZ(b, dst, dstLen);
    if (!CopyFileW(a, b, FALSE))
    {
        if (gle) *gle = GetLastError();
        return 0;
    }
    return 1;
}

static size_t CliMainDest(DimeCliHost*, int32_t mode, uint16_t* out, size_t cap)
{
    const wchar_t* name = nullptr;
    switch ((IME_MODE)mode)
    {
    case IME_MODE::IME_MODE_DAYI:     name = L"Dayi.cin"; break;
    case IME_MODE::IME_MODE_ARRAY:    name = L"Array.cin"; break;
    case IME_MODE::IME_MODE_PHONETIC: name = L"Phone.cin"; break;
    case IME_MODE::IME_MODE_GENERIC:  name = L"Generic.cin"; break;
    default: return 0;
    }
    WCHAR dime[MAX_PATH], dest[MAX_PATH];
    GetDimePath(dime, MAX_PATH);
    StringCchPrintfW(dest, MAX_PATH, L"%s\\%s", dime, name);
    return CopyOut(out, cap, dest);
}

static size_t CliPhraseDest(DimeCliHost*, uint16_t* out, size_t cap)
{
    WCHAR dime[MAX_PATH], dest[MAX_PATH];
    GetDimePath(dime, MAX_PATH);
    StringCchPrintfW(dest, MAX_PATH, L"%s\\Phrase.cin", dime);
    return CopyOut(out, cap, dest);
}

static size_t CliArrayDest(DimeCliHost*, int32_t table, uint16_t* out, size_t cap)
{
    const wchar_t* name = nullptr;
    switch (table)
    {
    case CLI_TABLE_SP:      name = L"Array-special.cin"; break;
    case CLI_TABLE_SC:      name = L"Array-shortcode.cin"; break;
    case CLI_TABLE_EXT_B:   name = L"Array-Ext-B.cin"; break;
    case CLI_TABLE_EXT_CD:  name = L"Array-Ext-CD.cin"; break;
    case CLI_TABLE_EXT_EFG: name = L"Array-Ext-EF.cin"; break;
    case CLI_TABLE_ARRAY40: name = L"Array40.cin"; break;
    case CLI_TABLE_PHRASE:  name = L"Array-Phrase.cin"; break;
    default: return 0;
    }
    WCHAR dime[MAX_PATH], dest[MAX_PATH];
    GetDimePath(dime, MAX_PATH);
    StringCchPrintfW(dest, MAX_PATH, L"%s\\%s", dime, name);
    return CopyOut(out, cap, dest);
}

} // extern "C"

static const DimeCliHostVtbl kCliVtbl =
{
    CliWriteOut,
    CliWriteErr,
    CliLoadConfig,
    CliWriteConfig,
    CliReset,
    CliGetValue,
    CliSetValue,
    CliFileExists,
    CliLoadText,
    CliFreeText,
    CliBeginValidate,
    CliValidateLine,
    CliEndValidate,
    CliCustomTxt,
    CliCustomCin,
    CliWriteUtf16,
    CliParseCin,
    CliExportSrc,
    CliCopyFile,
    CliMainDest,
    CliPhraseDest,
    CliArrayDest,
};

struct CliArgv
{
    LPWSTR* argv;
    int argc;
    const uint16_t** ptrs;
    size_t* lens;
};

static void CliArgvFree(CliArgv* o)
{
    if (o->ptrs) HeapFree(GetProcessHeap(), 0, o->ptrs);
    if (o->lens) HeapFree(GetProcessHeap(), 0, o->lens);
    if (o->argv) LocalFree(o->argv);
    o->ptrs = nullptr;
    o->lens = nullptr;
    o->argv = nullptr;
}

static bool CliArgvSplit(const wchar_t* cmd, CliArgv* o)
{
    o->argv = nullptr;
    o->ptrs = nullptr;
    o->lens = nullptr;
    o->argc = 0;
    if (!cmd) return false;
    o->argv = CommandLineToArgvW(cmd, &o->argc);
    if (!o->argv) return false;
    if (o->argc > 0)
    {
        o->ptrs = (const uint16_t**)HeapAlloc(GetProcessHeap(), 0, sizeof(const uint16_t*) * (size_t)o->argc);
        o->lens = (size_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(size_t) * (size_t)o->argc);
        if (!o->ptrs || !o->lens) { CliArgvFree(o); return false; }
        for (int i = 0; i < o->argc; i++)
        {
            o->ptrs[i] = reinterpret_cast<const uint16_t*>(o->argv[i]);
            o->lens[i] = wcslen(o->argv[i]);
        }
    }
    return true;
}

static void CopyCliArgs(CLIArgs& a, const DimeCliArgs& r)
{
    a.command = (CLI_COMMAND)r.command;
    a.mode = (IME_MODE)r.mode;
    a.hasMode = r.has_mode ? TRUE : FALSE;
    a.jsonOutput = r.json_output ? TRUE : FALSE;
    a.silent = r.silent ? TRUE : FALSE;
    a.noValidate = r.no_validate ? TRUE : FALSE;
    memcpy(a.getKey, r.get_key, sizeof(a.getKey));
    int n = r.set_count;
    if (n < 0) n = 0;
    if (n > CLI_MAX_SET_PAIRS) n = CLI_MAX_SET_PAIRS;
    a.setCount = n;
    for (int i = 0; i < n; i++)
    {
        memcpy(a.setPairs[i].key, r.set_keys[i], sizeof(a.setPairs[i].key));
        memcpy(a.setPairs[i].value, r.set_values[i], sizeof(a.setPairs[i].value));
    }
    memcpy(a.filePath, r.file_path, sizeof(a.filePath));
    a.arrayTable = (CLI_ARRAY_TABLE)r.array_table;
}

bool ParseCLIArgs(_In_ const wchar_t* cmdLine, _Out_ CLIArgs& args, _In_ FILE* err)
{
    ZeroMemory(&args, sizeof(args));
    args.mode = IME_MODE::IME_MODE_NONE;
    args.arrayTable = CLI_TABLE_NONE;
    CliArgv split;
    if (!CliArgvSplit(cmdLine, &split)) return false;
    DimeCliArgs raw = {};
    DimeBuffer* error = nullptr;
    int ok = dime_cli_parse(split.ptrs, split.lens, (size_t)split.argc, &raw, &error);
    if (error)
    {
        size_t n = 0;
        const uint16_t* p = dime_buffer_data(error, &n);
        if (err && p && n) WriteWide(err, p, n);
        dime_buffer_free(error);
    }
    CliArgvFree(&split);
    if (!ok) return false;
    CopyCliArgs(args, raw);
    return true;
}

// cppHost: the C++ host above (kCliVtbl) does everything; otherwise the Rust
// host (dime-ffi settings_cli.rs, phase 5b-15e) does, and only the two
// output streams come back here.
static int RunCLIWith(const wchar_t* cmdLine, FILE* out, FILE* err, bool cppHost)
{
    CliArgv split;
    if (!CliArgvSplit(cmdLine, &split)) return 1;
    CliRt rt = {};
    rt.out = out ? out : stdout;
    rt.err = err ? err : stderr;
    DimeCliHost host;
    host.vtbl = &kCliVtbl;
    host.ctx = &rt;
    int rc;
    if (cppHost) {
        rc = (int)dime_cli_run(split.ptrs, split.lens, (size_t)split.argc, &host);
    } else {
        const uint16_t* appData = nullptr;
#ifdef DIME_UNIT_TESTING
        if (Global::testAppDataOverride[0]) appData = reinterpret_cast<const uint16_t*>(Global::testAppDataOverride);
#endif
        rc = (int)dime_cli_run_native(split.ptrs, split.lens, (size_t)split.argc, &host, appData);
    }
    if (rt.engine) SettingsModel::DestroyEngine(rt.engine);
    CliArgvFree(&split);
    return rc;
}

int RunCLI(_In_ const wchar_t* cmdLine, _In_ FILE* out)
{
    return RunCLIWith(cmdLine, out, nullptr, false);
}

#ifdef DIME_UNIT_TESTING
int RunCLIForTest(const wchar_t* cmdLine, FILE* out, FILE* err, bool cppHost)
{
    return RunCLIWith(cmdLine, out, err, cppHost);
}

void WriteWideForTest(FILE* f, const wchar_t* text, size_t len)
{
    WriteWide(f, reinterpret_cast<const uint16_t*>(text), len);
}
#endif

// ---------------------------------------------------------------------------
// Unit-test shims (only compiled when DIME_UNIT_TESTING is defined)
// ---------------------------------------------------------------------------
#ifdef DIME_UNIT_TESTING
const KeyInfo* CLI_FindKey(const wchar_t* name)            { return FindKey(name); }
bool CLI_KeyApplicableToMode(const KeyInfo* ki, IME_MODE m){ return KeyApplicableToMode(ki, m); }
bool CLI_ParseColorValue(const wchar_t* str, COLORREF& out){ return ParseColorValue(str, out); }
int  CLI_GetKeyCount()                                      { return g_keyCount; }
const KeyInfo* CLI_GetKeyRegistry()                         { return g_keyRegistry; }
bool CLI_GetKeyValue(const KeyInfo* ki, WCHAR* outBuf, int bufLen) { return GetKeyValue(ki, outBuf, bufLen); }
int  CLI_ApplyKeyValue(const KeyInfo* ki, const wchar_t* valueStr, FILE* err) { return ApplyKeyValue(ki, valueStr, err); }
#endif

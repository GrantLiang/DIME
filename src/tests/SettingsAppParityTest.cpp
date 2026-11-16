/********************************************************************************
* SettingsAppParityTest: the settings app's configuration, C++
* (SettingsApp::ConfigHost over CConfig) vs Rust (dime-ffi settings_app.rs,
* phase 5b-15a), both driven through DimeSettingsAppVtbl.
*
* Each side gets its own temporary AppData folder and the same starting
* state (settings values, CConfig statics, reverse-conversion list), then
* the same random operations: LoadConfig / WriteConfig / ResetAllDefaults
* for every mode, colour modes and palettes, SetColor, snapshots, reverse
* selections, and INI files written or deleted behind the app's back.
* After every step the return values, the settings values, the six live
* colours, the snapshot, the paths and every INI file must match. The two
* sides run one after the other: with DIME_USE_RUST_CONFIG, CConfig's values
* are the same Rust object the Rust app uses.
*
* File times: after each step a changed INI file gets "base + step" as its
* modification time and an unchanged one keeps its old time, on both sides,
* so "the file was updated by someone else" does not depend on the clock.
*
* Golden files (src/tests/golden/SettingsApp): the Rust side's state after
* every step (hashed in chunks; the machine's dark mode is masked) and per
* engine scenario step. They assume the default 3D highlight colour (white),
* which the defaults are built from; SystemFacts reads the machine and has
* no golden file.
*
* Source is ASCII only (the project compiles with code page 950).
********************************************************************************/
#include "pch.h"

#ifdef DIME_RUST_AVAILABLE

#include "CppUnitTest.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../Config.h"
#include "../Define.h"
#include "../DIMESettings/SettingsApp.h"
#include "../DIMESettings/SettingsController.h"
#include "dime_ffi.h"
#include "SettingsAppTestAccess.h"
#include "Golden.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    using Access = DIMEUnitTests::SettingsAppConfigAccess;

    const wchar_t* const kIniNames[] = { L"DayiConfig.ini", L"ArrayConfig.ini", L"PhoneConfig.ini",
        L"GenericConfig.ini", L"config.ini" };

    // A reverse-conversion provider of the test list.
    struct Provider {
        GUID clsid, profile;
        std::wstring description;
    };

    std::vector<Provider> TestProviders()
    {
        // The second entry has the Dayi profile: LoadConfig(Dayi) drops it.
        const GUID dayi = { 0x36851834, 0x92ad, 0x4397, { 0x9f, 0x50, 0x80, 0x3, 0x84, 0xd5, 0xc2, 0x4c } };
        std::vector<Provider> out;
        out.push_back({ { 0x11111111, 1, 2, { 1, 2, 3, 4, 5, 6, 7, 8 } }, { 0x22222222, 3, 4, { 8, 7, 6, 5, 4, 3, 2, 1 } },
            std::wstring(1, (wchar_t)0x5009) + L" one" });
        out.push_back({ { 0x33333333, 5, 6, { 9, 9, 9, 9, 9, 9, 9, 9 } }, dayi, L"dayi itself" });
        out.push_back({ { 0x44444444, 7, 8, { 1, 1, 1, 1, 1, 1, 1, 1 } }, { 0x55555555, 9, 10, { 2, 2, 2, 2, 2, 2, 2, 2 } },
            L"three" });
        return out;
    }

    std::wstring Hex(const void* p, size_t n)
    {
        static const wchar_t* d = L"0123456789abcdef";
        std::wstring s;
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; i++) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
        return s;
    }

    std::string ReadBytes(const std::wstring& path)
    {
        std::string out;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return out;
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
        CloseHandle(h);
        return out;
    }

    void WriteBytes(const std::wstring& path, const std::string& bytes)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(h != INVALID_HANDLE_VALUE, path.c_str());
        DWORD n = 0;
        if (!bytes.empty()) WriteFile(h, bytes.data(), (DWORD)bytes.size(), &n, nullptr);
        CloseHandle(h);
    }

    bool GetWriteTime(const std::wstring& path, FILETIME& ft)
    {
        WIN32_FILE_ATTRIBUTE_DATA a = {};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return false;
        ft = a.ftLastWriteTime;
        return true;
    }

    void SetWriteTime(const std::wstring& path, const FILETIME& ft)
    {
        HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        SetFileTime(h, nullptr, nullptr, &ft);
        CloseHandle(h);
    }

    std::string Utf16File(const std::wstring& text)
    {
        std::string s("\xFF\xFE", 2);
        s.append((const char*)text.data(), text.size() * sizeof(wchar_t));
        return s;
    }

    // One operation, replayed on both sides.
    struct Op {
        int kind = 0, a = 0, b = 0;
        uint32_t c = 0;
        std::wstring Name() const
        {
            return L"op " + std::to_wstring(kind) + L"(" + std::to_wstring(a) + L"," + std::to_wstring(b) + L","
                + std::to_wstring(c) + L")";
        }
    };

    std::vector<Op> RandomOps(unsigned seed, int count)
    {
        std::mt19937 rng(seed);
        auto R = [&](int lo, int hi) { return Golden::Pick(rng, lo, hi); };
        std::vector<Op> out;
        for (int i = 0; i < count; i++) {
            Op op;
            static const int kinds[] = { 0, 0, 0, 1, 1, 2, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11 };
            op.kind = kinds[R(0, (int)_countof(kinds) - 1)];
            op.a = R(0, 4);
            op.b = R(0, 7);
            op.c = (uint32_t)rng() & 0xFFFFFF;
            out.push_back(op);
        }
        return out;
    }

    struct Side {
        bool rust;
        std::wstring dir;
        const DimeSettingsAppVtbl* v = nullptr;
        void* ctx = nullptr;
        SettingsApp::ConfigHost cpp;     // the C++ host (and the Rust app's fallback)
        void* native = nullptr;
        std::vector<std::wstring> log;

        std::wstring Ini(int i) const { return dir + L"\\DIME\\" + kIniNames[i]; }

        std::wstring Norm(std::wstring s) const
        {
            for (size_t p; (p = s.find(dir)) != std::wstring::npos;) s.replace(p, dir.size(), L"<dir>");
            return s;
        }

        std::wstring Str(size_t (*f)(void*, int32_t, uint16_t*, size_t), int arg)
        {
            uint16_t buf[1024] = {};
            size_t n = f(ctx, arg, buf, 1024);
            return Norm(std::wstring((const wchar_t*)buf, n < 1024 ? n : 1023));
        }

        // Values, description, colours, snapshot, reverse list, paths, files.
        std::wstring State()
        {
            DimeConfigValues vals = {};
            std::wstring desc = L"<null>";
            if (rust) {
                dime_settings_get_values(dime_settings_global(), &vals);
                const uint16_t* t = nullptr;
                size_t n = 0;
                if (dime_settings_get_description(dime_settings_global(), &t, &n)) desc.assign((const wchar_t*)t, n);
            } else {
                CConfig::ExportValues(&vals);
                if (WCHAR* d = CConfig::GetReverseConversionDescription()) desc = d;
            }
            bool end = false;
            for (uint16_t& ch : vals.font_face_name) { if (end) ch = 0; else if (ch == 0) end = true; }
            std::wstring s = L"values " + Hex(&vals, sizeof(vals)) + L"\ndesc [" + desc + L"]\n";
            uint32_t colors[6] = {};
            v->get_colors(ctx, colors);
            s += L"colors " + Hex(colors, sizeof(colors)) + L" mode " + std::to_wstring(v->color_mode(ctx)) + L"\n";
            SettingsSnapshot snap = {};
            v->load_snapshot(ctx, &snap);
            s += L"snapshot " + Hex(&snap, sizeof(snap)) + L"\n";
            int n = v->reverse_count(ctx);
            s += L"reverse " + std::to_wstring(n) + L" null " + std::to_wstring(v->reverse_is_null(ctx));
            for (int i = 0; i < n; i++)
                s += L" [" + Str(v->reverse_description, i) + L" cur " + std::to_wstring(v->reverse_is_current(ctx, i)) + L"]";
            s += L"\n";
            for (int m = 0; m < 4; m++)
                s += L"paths " + Str(v->custom_table_txt_path, m) + L" " + Str(v->custom_table_cin_path, m) + L"\n";
            uint16_t buf[1024] = {};
            size_t len = v->app_data_path(ctx, buf, 1024);
            s += L"appdata " + Norm(std::wstring((const wchar_t*)buf, len)) + L" 1809 " + std::to_wstring(v->windows_1809_or_later(ctx)) + L"\n";
            for (int i = 0; i < 5; i++) {
                std::string b = ReadBytes(Ini(i));
                DWORD attr = GetFileAttributesW(Ini(i).c_str());
                s += std::wstring(kIniNames[i]) + L" " + (attr == INVALID_FILE_ATTRIBUTES ? L"-" : Hex(b.data(), b.size())) + L"\n";
            }
            return s;
        }
    };

    // The same starting point for each side.
    void ResetCommon(const DimeConfigEnv& env, const std::vector<Provider>& providers)
    {
        DimeSettings* fresh = dime_settings_new(nullptr, &env);
        DimeConfigValues v = {};
        dime_settings_get_values(fresh, &v);
        dime_settings_free(fresh);
        CConfig::ImportValues(&v);
        CConfig::SetReverseConversionDescriptionText(nullptr);
        dime_settings_set_env(dime_settings_global(), &env);
        dime_settings_set_values(dime_settings_global(), &v);
        dime_settings_set_description(dime_settings_global(), nullptr, 0, 0);
        Access::Reset();
        CDIMEArray<LanguageProfileInfo> list;
        for (const Provider& p : providers) {
            LanguageProfileInfo* info = list.Append();
            info->clsid = p.clsid;
            info->guidProfile = p.profile;
            info->description = const_cast<WCHAR*>(p.description.c_str());
        }
        CConfig::SetReverseConvervsionInfoList(&list);
    }

    void Run(Side& s, const std::vector<Op>& ops, const DimeConfigEnv& env, const std::vector<Provider>& providers)
    {
        CreateDirectoryW(s.dir.c_str(), nullptr);
        ResetCommon(env, providers);
        wcscpy_s(Global::testAppDataOverride, s.dir.c_str());
        if (s.rust) {
            s.native = dime_settings_native_app_new(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
                static_cast<SettingsApp::Host*>(&s.cpp), (const uint16_t*)s.dir.c_str());
            Assert::IsNotNull(s.native);
            s.v = dime_settings_native_app_vtbl();
            s.ctx = s.native;
            std::vector<std::array<uint8_t, 16>> clsids, profiles;
            std::vector<const uint16_t*> descs;
            for (const Provider& p : providers) {
                std::array<uint8_t, 16> a, b;
                memcpy(a.data(), &p.clsid, 16);
                memcpy(b.data(), &p.profile, 16);
                clsids.push_back(a);
                profiles.push_back(b);
                descs.push_back((const uint16_t*)p.description.c_str());
            }
            dime_settings_native_app_set_reverse(s.native, providers.size(),
                reinterpret_cast<const uint8_t(*)[16]>(clsids.data()), reinterpret_cast<const uint8_t(*)[16]>(profiles.data()),
                descs.data());
        } else {
            s.v = static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl());
            s.ctx = static_cast<SettingsApp::Host*>(&s.cpp);
        }

        FILETIME base = {};
        SYSTEMTIME st = { 2031, 1, 0, 1, 0, 0, 0, 0 };
        SystemTimeToFileTime(&st, &base);
        auto at = [&](int step) {
            ULARGE_INTEGER u = { { base.dwLowDateTime, base.dwHighDateTime } };
            u.QuadPart += (ULONGLONG)step * 20000000ull;   // 2 s per step
            FILETIME f = { u.LowPart, u.HighPart };
            return f;
        };

        for (size_t i = 0; i < ops.size(); i++) {
            const Op& op = ops[i];
            // Files and times before the step.
            std::string before[5];
            bool existed[5];
            FILETIME times[5] = {};
            for (int f = 0; f < 5; f++) {
                existed[f] = GetFileAttributesW(s.Ini(f).c_str()) != INVALID_FILE_ATTRIBUTES;
                before[f] = ReadBytes(s.Ini(f));
                GetWriteTime(s.Ini(f), times[f]);
            }
            std::wstring result;
            switch (op.kind) {
            case 0: s.v->load_config(s.ctx, op.a); break;
            case 1: s.v->write_config(s.ctx, op.a); break;
            case 2: s.v->reset_all_defaults(s.ctx, op.a); break;
            case 3: s.v->set_color_mode(s.ctx, op.a % 4); break;
            case 4: s.v->load_colors_for_mode(s.ctx, op.a % 4); break;
            case 5: s.v->save_colors_for_mode(s.ctx, op.a % 4); break;
            case 6: s.v->set_color(s.ctx, op.b % 6, op.c); break;
            case 7: {
                SettingsSnapshot snap = {};
                s.v->load_snapshot(s.ctx, &snap);
                uint8_t* p = reinterpret_cast<uint8_t*>(&snap);
                p[(op.c >> 8) % sizeof(snap)] ^= (uint8_t)(1 << op.b);
                s.v->apply_snapshot(s.ctx, &snap);
                break;
            }
            case 8: s.v->set_reverse_selection(s.ctx, op.a % (s.v->reverse_count(s.ctx) + 1)); break;
            case 9: {
                // An INI file written by someone else.
                std::wstring text;
                switch (op.b % 4) {
                case 0: text = L"[Config]\r\nMaxCodes = " + std::to_wstring(op.c % 9) + L"\r\nColorMode = "
                    + std::to_wstring(op.c % 5) + L"\r\nItemColor = 0x" + std::to_wstring(op.c % 1000) + L"\r\n"; break;
                case 1: text = L"[Config]\r\nAutoCompose = 1\r\nFontSize = " + std::to_wstring(op.c % 40)
                    + L"\r\nAppPermissionSet = " + std::to_wstring(op.c & 1) + L"\r\nDarkItemColor = 0x00FF00\r\n"; break;
                case 2: text = L"no section\r\nMaxCodes = 3\r\n"; break;
                default: text.clear(); break;
                }
                std::string bytes = (op.b % 4 == 3) ? std::string("\x01\x02\x03", (op.c % 4)) : Utf16File(text);
                CreateDirectoryW((s.dir + L"\\DIME").c_str(), nullptr);
                WriteBytes(s.Ini(op.a), bytes);
                break;
            }
            case 10: DeleteFileW(s.Ini(op.a).c_str()); break;
            default: {
                // Getters only.
                result = L"dark " + std::to_wstring(s.v->system_dark(s.ctx));
                break;
            }
            }
            // Deterministic file times.
            for (int f = 0; f < 5; f++) {
                if (GetFileAttributesW(s.Ini(f).c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                std::string now = ReadBytes(s.Ini(f));
                if (!existed[f] || now != before[f]) SetWriteTime(s.Ini(f), at((int)i + 1));
                else SetWriteTime(s.Ini(f), times[f]);
            }
            s.log.push_back(op.Name() + L" " + result + L"\n" + s.State());
        }
        if (s.native) dime_settings_native_app_free(s.native);
        s.native = nullptr;
    }

    // ---- EngineKeys -------------------------------------------------------

    std::wstring RepoRoot()
    {
        std::wstring p = __FILEW__;            // ...\src\tests\SettingsAppParityTest.cpp
        for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
        return p + L"\\";
    }

    void RemoveTree(const std::wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                std::wstring p = dir + L"\\" + n;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(p);
                else DeleteFileW(p.c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(dir.c_str());
    }

    // Names and sizes under `dir`, sorted.
    void Listing(const std::wstring& dir, const std::wstring& rel, std::vector<std::wstring>& out)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Listing(dir + L"\\" + n, rel + n + L"\\", out);
            else out.push_back(rel + n + L" " + std::to_wstring(fd.nFileSizeLow));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(out.begin(), out.end());
    }

    // A small table: %keyname with `keys`, %endkey, maybe %autoCompose.
    std::string Table(const std::wstring& keys, const std::wstring& endkey, int autoCompose, bool utf16 = true)
    {
        // The converted (parseCINFile) form the IME loads: tabs, quoted keys.
        auto q = [](const std::wstring& s) {
            std::wstring o = L"\"";
            for (wchar_t c : s) { if (c == L'"' || c == L'\\') o += L'\\'; o += c; }
            return o + L"\"";
        };
        std::wstring t = L"%gen_inp\r\n%ename\ttest\r\n";
        if (!endkey.empty()) t += L"%endkey\t" + endkey + L"\r\n";
        if (autoCompose >= 0) t += L"%autoCompose\t" + std::to_wstring(autoCompose) + L"\r\n";
        t += L"%keyname begin\r\n";
        for (wchar_t k : keys) t += q(std::wstring(1, k)) + L"\t" + q(std::wstring(1, (wchar_t)0x4E00 + (k % 50))) + L"\r\n";
        t += L"%keyname end\r\n%chardef begin\r\n";
        if (!keys.empty()) t += q(std::wstring(1, keys[0])) + L"\t" + q(std::wstring(1, (wchar_t)0x4E2D)) + L"\r\n";
        t += L"%chardef end\r\n";
        if (!utf16) {
            std::string s;
            for (wchar_t c : t) s += (char)(c < 128 ? c : '?');
            return s;
        }
        return Utf16File(t);
    }

    struct EngineScenario {
        std::wstring name;
        // Files under AppData\DIME and ProgramW6432\DIME (name -> bytes, or a
        // repo path prefixed with '@').
        std::vector<std::pair<std::wstring, std::string>> app, pf;
        int arrayScope;
        std::vector<int> modes;   // RecreateEngine order; -1 = DestroyEngine
    };

    std::vector<EngineScenario> EngineScenarios()
    {
        std::wstring abc = L"abcdefghijklmnopqrstuvwxyz";
        std::wstring dayi = L"abcdefghijklmnopqrstuvwxyz,./;'[]-=\\`";
        std::wstring many;
        for (wchar_t c = 33; c < 33 + 97; c++) many += c;   // 97 radicals: too many
        std::vector<EngineScenario> out;
        out.push_back({ L"nothing", {}, {}, 0, { 0, 1, 2, 3 } });
        out.push_back({ L"dayi in appdata", { { L"Dayi.cin", Table(dayi, L"", -1) } }, {}, 0, { 0, -1, 0 } });
        out.push_back({ L"dayi with '=' and endkey", { { L"Dayi.cin", Table(L"=abc,.", L" ,.", 0) } }, {}, 0, { 0 } });
        out.push_back({ L"array shipped", {}, { { L"Array.cin", "@installer\\Array.cin" }, { L"Array-special.cin", "@installer\\Array-special.cin" },
            { L"Array-shortcode.cin", "@installer\\Array-shortcode.cin" } }, 0, { 1, 1 } });
        out.push_back({ L"array40 shipped", {}, { { L"Array40.cin", "@installer\\Array40.cin" }, { L"Array.cin", "@installer\\Array.cin" } }, 4, { 1 } });
        out.push_back({ L"array appdata newer than shipped", { { L"Array.cin", Table(abc + L",./;", L"", 1) } },
            { { L"Array.cin", Table(L"abc", L"", 0) } }, 0, { 1 } });
        out.push_back({ L"phonetic shipped", {}, { { L"Phone.cin", "@installer\\phone.cin" } }, 0, { 2, 0, 2 } });
        out.push_back({ L"generic", { { L"Generic.cin", Table(abc, L"1234", 0) } }, {}, 0, { 3 } });
        out.push_back({ L"generic lower and high keys", { { L"Generic.cin", Table(L"aBz~" + std::wstring(1, (wchar_t)0xE9), L"z", -1) } }, {}, 0, { 3 } });
        out.push_back({ L"too many radicals", { { L"Generic.cin", Table(many, L"", -1) } }, {}, 0, { 3 } });
        out.push_back({ L"not unicode", { { L"Generic.cin", Table(abc, L"", 1, false) } }, {}, 0, { 3 } });
        out.push_back({ L"custom tables with autoCompose", { { L"Generic.cin", Table(abc, L"", 0) },
            { L"GENERIC-CUSTOM.cin", Table(L"ab", L"", 1) }, { L"DAYI-CUSTOM.cin", Table(L"ab", L"", 0) },
            { L"Dayi.cin", Table(dayi, L"", 1) }, { L"Phrase.cin", Table(L"xy", L"", 0) } }, {}, 0, { 3, 0, 1, 2 } });
        return out;
    }

    // Load the files of a scenario into `app` / `pf`.
    void Place(const std::wstring& root, const std::vector<std::pair<std::wstring, std::string>>& files, WORD year)
    {
        SYSTEMTIME st = { year, 1, 0, 1, 0, 0, 0, 0 };
        FILETIME ft = {};
        SystemTimeToFileTime(&st, &ft);
        CreateDirectoryW(root.c_str(), nullptr);
        CreateDirectoryW((root + L"\\DIME").c_str(), nullptr);
        for (const auto& f : files) {
            std::wstring dst = root + L"\\DIME\\" + f.first;
            if (!f.second.empty() && f.second[0] == '@') {
                std::wstring src = RepoRoot() + std::wstring(f.second.begin() + 1, f.second.end());
                Assert::IsTrue(CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0, src.c_str());
            } else {
                WriteBytes(dst, f.second);
            }
            SetWriteTime(dst, ft);
        }
    }

    std::vector<std::wstring> RunEngine(bool rust, const EngineScenario& sc, const std::wstring& base, const DimeConfigEnv& env)
    {
        std::wstring app = base + (rust ? L"\\appR" : L"\\appC"), pf = base + (rust ? L"\\pfR" : L"\\pfC");
        RemoveTree(app);
        RemoveTree(pf);
        // Fixed file times: shipped tables are newer than the AppData ones.
        Place(app, sc.app, 2020);
        Place(pf, sc.pf, 2025);
        ResetCommon(env, {});
        CConfig::SetArrayScope((ARRAY_SCOPE)sc.arrayScope);
        DimeConfigValues v = {};
        CConfig::ExportValues(&v);
        dime_settings_set_values(dime_settings_global(), &v);
        wcscpy_s(Global::testAppDataOverride, app.c_str());
        SetEnvironmentVariableW(L"ProgramW6432", pf.c_str());
        Global::imeMode = IME_MODE::IME_MODE_NONE;

        SettingsApp::ConfigHost cpp;
        void* native = nullptr;
        const DimeSettingsAppVtbl* vt = static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl());
        void* ctx = static_cast<SettingsApp::Host*>(&cpp);
        if (rust) {
            native = dime_settings_native_app_new(vt, ctx, (const uint16_t*)app.c_str());
            vt = dime_settings_native_app_vtbl();
            ctx = native;
        }
        std::vector<std::wstring> log;
        for (int m : sc.modes) {
            if (m < 0) vt->destroy_engine(ctx);
            else vt->recreate_engine(ctx, m);
            std::wstring keys;
            for (int c = 0; c < 0x180; c++) keys += vt->engine_char_ok(ctx, (uint16_t)c) ? L'1' : L'0';
            DimeConfigValues vals = {};
            if (rust) dime_settings_get_values(dime_settings_global(), &vals);
            else CConfig::ExportValues(&vals);
            bool end = false;
            for (uint16_t& ch : vals.font_face_name) { if (end) ch = 0; else if (ch == 0) end = true; }
            std::vector<std::wstring> files;
            Listing(app, L"", files);
            std::wstring s = sc.name + L" mode " + std::to_wstring(m) + L" engine " + std::to_wstring(vt->has_engine(ctx))
                + L"\nkeys " + keys + L"\nvalues " + Hex(&vals, sizeof(vals)) + L"\nfiles:";
            for (const std::wstring& f : files) s += L" [" + f + L"]";
            log.push_back(s);
        }
        vt->destroy_engine(ctx);
        if (native) dime_settings_native_app_free(native);
        RemoveTree(app);
        RemoveTree(pf);
        return log;
    }

    void Cleanup(const std::wstring& dir)
    {
        for (int i = 0; i < 5; i++) DeleteFileW((dir + L"\\DIME\\" + kIniNames[i]).c_str());
        RemoveDirectoryW((dir + L"\\DIME").c_str());
        RemoveDirectoryW(dir.c_str());
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsAppParityTest)
    {
        DimeConfigValues _savedValues = {};
        std::wstring _savedDescription;
        bool _savedHasDescription = false;
        SettingsAppConfigAccess::Saved _savedStatics = {};
        WCHAR _savedAppData[MAX_PATH] = {};
        BOOL _saved1809 = FALSE;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            CConfig::ExportValues(&_savedValues);
            WCHAR* d = CConfig::GetReverseConversionDescription();
            _savedHasDescription = d != nullptr;
            _savedDescription = d ? d : L"";
            _savedStatics = SettingsAppConfigAccess::Save();
            wcscpy_s(_savedAppData, Global::testAppDataOverride);
            // The value DllMain / wWinMain set on Windows 10 1809+; tests that
            // run earlier may leave either value, and the defaults (colour
            // mode) recorded in the golden files depend on it.
            _saved1809 = Global::isWindows1809OrLater;
            Global::isWindows1809OrLater = TRUE;
        }

        TEST_METHOD_CLEANUP(Teardown)
        {
            CConfig::ImportValues(&_savedValues);
            CConfig::SetReverseConversionDescriptionText(_savedHasDescription ? _savedDescription.c_str() : nullptr);
            SettingsAppConfigAccess::Restore(_savedStatics);
            CDIMEArray<LanguageProfileInfo> empty;
            CConfig::SetReverseConvervsionInfoList(&empty);
            wcscpy_s(Global::testAppDataOverride, _savedAppData);
            Global::isWindows1809OrLater = _saved1809;
        }

        // Random operations: every step matches.
        TEST_METHOD(RandomOperations)
        {
            // The C++ side reads these itself, so both sides use the machine's
            // values; the golden file assumes the defaults (a white 3D
            // highlight colour and the test process's 1809 flag).
            DimeConfigEnv env = { GetSysColor(COLOR_3DHIGHLIGHT), Global::isWindows1809OrLater ? 1 : 0 };
            std::vector<Provider> providers = TestProviders();
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            int loads = 0;
            Golden::Log golden;
            for (unsigned seed = 1; seed <= 6; seed++) {
                std::vector<Op> ops = RandomOps(seed, 250);
                for (const Op& op : ops) if (op.kind == 0) loads++;
                std::wstring base = std::wstring(tmp) + L"DIMEAppParity" + std::to_wstring(GetCurrentProcessId());
                Side c, r;
                c.rust = false;
                r.rust = true;
                c.dir = base + L"C";
                r.dir = base + L"R";
                Cleanup(c.dir);
                Cleanup(r.dir);
                Run(c, ops, env, providers);
                Run(r, ops, env, providers);
                Cleanup(c.dir);
                Cleanup(r.dir);
                for (size_t i = 0; i < ops.size(); i++) {
                    if (c.log[i] == r.log[i]) continue;
                    // Only the lines that differ.
                    std::wstring diff;
                    size_t pc = 0, pr = 0;
                    while (pc < c.log[i].size() || pr < r.log[i].size()) {
                        size_t ec = c.log[i].find(L'\n', pc), er = r.log[i].find(L'\n', pr);
                        std::wstring lc = c.log[i].substr(pc, ec == std::wstring::npos ? std::wstring::npos : ec - pc);
                        std::wstring lr = r.log[i].substr(pr, er == std::wstring::npos ? std::wstring::npos : er - pr);
                        if (lc != lr) diff += L"C++:  " + lc.substr(0, 600) + L"\nRust: " + lr.substr(0, 600) + L"\n";
                        pc = ec == std::wstring::npos ? c.log[i].size() : ec + 1;
                        pr = er == std::wstring::npos ? r.log[i].size() : er + 1;
                    }
                    std::wstring history;
                    for (size_t k = (i > 6 ? i - 6 : 0); k <= i; k++) history += ops[k].Name() + L"; ";
                    Assert::Fail((L"seed " + std::to_wstring(seed) + L" step " + std::to_wstring(i) + L"\nops: " + history
                        + L"\n" + diff).c_str());
                }
                Golden::Chunks ch(golden, L"seed " + std::to_wstring(seed) + L" steps", 25);
                for (const std::wstring& step : r.log) {
                    // The machine's dark mode (op 11) is not part of the golden file.
                    ch.Add(Golden::Replace(Golden::Replace(step, L" dark 1\n", L" dark ?\n"), L" dark 0\n", L" dark ?\n"));
                    ch.Next();
                }
                ch.Finish();
            }
            Golden::Check(L"SettingsApp", L"RandomOperations", golden);
            Assert::IsTrue(loads > 100, std::to_wstring(loads).c_str());
        }

        // The engine the custom table check uses (phase 5b-15c): which
        // characters are radical keys, the settings it changes, and the
        // tables it copies into AppData, for each scenario.
        TEST_METHOD(EngineKeys)
        {
            DimeConfigEnv env = { GetSysColor(COLOR_3DHIGHLIGHT), Global::isWindows1809OrLater ? 1 : 0 };
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            std::wstring base = std::wstring(tmp) + L"DIMEEngineParity" + std::to_wstring(GetCurrentProcessId());
            CreateDirectoryW(base.c_str(), nullptr);
            wchar_t savedPf[MAX_PATH] = {};
            DWORD hadPf = GetEnvironmentVariableW(L"ProgramW6432", savedPf, MAX_PATH);
            IME_MODE savedMode = Global::imeMode;
            int keyed = 0;
            std::wstring failure;
            Golden::Log golden;
            for (const EngineScenario& sc : EngineScenarios()) {
                std::vector<std::wstring> c = RunEngine(false, sc, base, env);
                std::vector<std::wstring> r = RunEngine(true, sc, base, env);
                for (const std::wstring& step : r) {
                    Golden::Hash h;
                    h.Add(step);
                    golden.Add(step.substr(0, step.find(L'\n')) + L" " + h.Hex());
                }
                for (size_t i = 0; i < c.size() && failure.empty(); i++) {
                    std::wstring cn = c[i], rn = r[i];
                    if (cn != rn) failure = L"C++:\n" + cn + L"\nRust:\n" + rn;
                    {
                        size_t k = cn.find(L"keys ");
                        std::wstring bits = cn.substr(k + 5, 0x180);
                        Logger::WriteMessage((cn.substr(0, cn.find(L'\n')) + L": " + std::to_wstring(std::count(bits.begin(), bits.end(), L'1'))
                            + L" keys; " + cn.substr(cn.find(L"files:")) + L"\n").c_str());
                    }
                    if (cn.find(L"keys ") != std::wstring::npos && cn.find(L'1', cn.find(L"keys ")) < cn.find(L"\nvalues")) keyed++;
                }
            }
            SetEnvironmentVariableW(L"ProgramW6432", hadPf ? savedPf : nullptr);
            Global::imeMode = savedMode;
            RemoveTree(base);
            Assert::IsTrue(failure.empty(), failure.c_str());
            Golden::Check(L"SettingsApp", L"EngineKeys", golden);
            Logger::WriteMessage((L"engine states with radical keys: " + std::to_wstring(keyed) + L"\n").c_str());
            Assert::IsTrue(keyed >= 8, std::to_wstring(keyed).c_str());
        }

        // What the app reads from the system is the same on both sides.
        TEST_METHOD(SystemFacts)
        {
            SettingsApp::ConfigHost cpp;
            void* native = dime_settings_native_app_new(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
                static_cast<SettingsApp::Host*>(&cpp), nullptr);
            Assert::IsNotNull(native);
            const DimeSettingsAppVtbl* cv = static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl());
            const DimeSettingsAppVtbl* rv = dime_settings_native_app_vtbl();
            void* cc = static_cast<SettingsApp::Host*>(&cpp);
            Assert::AreEqual(cv->dpi_for_window(cc, nullptr), rv->dpi_for_window(native, nullptr), L"dpi");
            Assert::AreEqual(cv->system_dark(cc), rv->system_dark(native), L"dark");
            // %APPDATA% without the test override.
            wchar_t saved[MAX_PATH];
            wcscpy_s(saved, Global::testAppDataOverride);
            Global::testAppDataOverride[0] = 0;
            uint16_t a[MAX_PATH] = {}, b[MAX_PATH] = {};
            size_t na = cv->app_data_path(cc, a, MAX_PATH), nb = rv->app_data_path(native, b, MAX_PATH);
            wcscpy_s(Global::testAppDataOverride, saved);
            Assert::AreEqual(std::wstring((const wchar_t*)a, na), std::wstring((const wchar_t*)b, nb), L"AppData");
            // The TSF reverse-conversion providers of this machine.
            CoInitialize(nullptr);
            cv->enumerate_reverse_providers(cc);
            rv->enumerate_reverse_providers(native);
            int n = cv->reverse_count(cc);
            Assert::AreEqual(n, rv->reverse_count(native), L"providers");
            for (int i = 0; i < n; i++) {
                uint16_t x[256] = {}, y[256] = {};
                cv->reverse_description(cc, i, x, 256);
                rv->reverse_description(native, i, y, 256);
                Assert::AreEqual(std::wstring((const wchar_t*)x), std::wstring((const wchar_t*)y), L"provider description");
            }
            Logger::WriteMessage((L"reverse-conversion providers: " + std::to_wstring(n) + L"\n").c_str());
            dime_settings_native_app_free(native);
            CoUninitialize();
        }
    };
}

#endif

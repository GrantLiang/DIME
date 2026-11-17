/********************************************************************************
* CliHostParityTest: the settings CLI on the C++ host (CLI.cpp's
* DimeCliHostVtbl over CConfig, the C++ engine and parseCINFile) vs the
* Rust host (dime-ffi settings_cli.rs, phase 5b-15e). The command logic is
* dime-config::cli on both sides; what differs is everything the commands
* do: config load / write / reset, --get / --set, custom table import with
* validation, export, table loading.
*
* Each side has its own AppData folder; every command starts from the
* values and CConfig statics a new DIMESettings.exe process has. After each
* command the exit code, stdout, stderr (folder names replaced by <dir>),
* every file under the side's AppData and the settings values must match.
*
* Golden file (src/tests/golden/CliHost): per command, a hash of the Rust
* host's result (temporary folder names replaced by <base>). It assumes the
* default 3D highlight colour (white), which the defaults are built from.
*
* Source is ASCII only (the project compiles with code page 950).
********************************************************************************/
#include "pch.h"


#include "CppUnitTest.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../Config.h"
#include "../DIMESettings/CLI.h"
#include "dime_ffi.h"
#include "SettingsAppTestAccess.h"
#include "Golden.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    using Access = DIMEUnitTests::SettingsAppConfigAccess;

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

    std::string Utf16(const std::wstring& s)
    {
        return std::string("\xFF\xFE", 2) + std::string((const char*)s.data(), s.size() * sizeof(wchar_t));
    }

    std::string Utf8(const std::wstring& s)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
        return out;
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

    std::wstring Hex(const std::string& b)
    {
        static const wchar_t* d = L"0123456789abcdef";
        std::wstring s;
        for (unsigned char c : b) { s += d[c >> 4]; s += d[c & 15]; }
        return s;
    }

    // Every file under `dir` (relative name and content), sorted.
    void Files(const std::wstring& dir, const std::wstring& rel, std::vector<std::wstring>& out)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Files(dir + L"\\" + n, rel + n + L"\\", out);
            else out.push_back(rel + n + L" " + Hex(ReadBytes(dir + L"\\" + n)));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        std::sort(out.begin(), out.end());
    }

    std::wstring Replace(std::wstring s, const std::wstring& from, const std::wstring& to)
    {
        for (size_t p = 0; (p = s.find(from, p)) != std::wstring::npos; p += to.size()) s.replace(p, from.size(), to);
        return s;
    }

    // The input files both sides read.
    void MakeInputs(const std::wstring& in)
    {
        CreateDirectoryW(in.c_str(), nullptr);
        std::wstring zhong(1, (wchar_t)0x4E2D), wen(1, (wchar_t)0x6587);
        WriteBytes(in + L"\\custom_ok.txt", Utf16(L"a " + zhong + L"\r\nab " + zhong + wen + L"\r\n"));
        WriteBytes(in + L"\\custom_utf8.txt", Utf8(L"abc " + zhong + L"\nq " + wen + L"\n"));
        WriteBytes(in + L"\\custom_bad.txt", Utf16(L"nospace\r\nabcdefghijk " + wen + L"\r\n%% " + zhong + L"\r\na " + zhong + L"\r\n"));
        WriteBytes(in + L"\\custom_empty.txt", "");
        std::wstring cin = L"%gen_inp\n%ename test\n%selkey 1234567890\n%endkey ,.\n%keyname begin\n";
        for (wchar_t k = L'a'; k <= L'z'; k++) cin += std::wstring(1, k) + L" " + std::wstring(1, (wchar_t)(0x4E00 + k)) + L"\n";
        cin += L", ,\n. .\n%keyname end\n%chardef begin\na " + zhong + L"\nab " + wen + L"\n%chardef end\n";
        WriteBytes(in + L"\\table.cin", Utf8(cin));
        WriteBytes(in + L"\\garbage.cin", std::string("\x01\x02", 2));
    }

    std::vector<std::wstring> Commands()
    {
        return {
            L"--help", L"--list-modes", L"--list-keys", L"--mode dayi --list-keys",
            L"--mode dayi --get-all", L"--mode array --get-all --json", L"--mode phonetic --get FontSize",
            L"--mode generic --get NoSuchKey", L"--get FontSize",
            L"--mode dayi --set FontSize 20", L"--mode dayi --get FontSize", L"--mode dayi --set FontSize abc",
            L"--mode array --set ArrayScope 4", L"--mode dayi --set ArrayScope 1",
            L"--mode dayi --set ColorMode 2 --set FontItalic 1", L"--mode dayi --get-all",
            L"--mode array --set MaxCodes 9", L"--mode array --get MaxCodes", L"--mode dayi --reset",
            L"--mode dayi --get-all --json", L"--mode array --reset --silent",
            L"--mode dayi --import-custom {in}\\custom_ok.txt",
            L"--mode dayi --load-main {in}\\table.cin", L"--mode dayi --import-custom {in}\\custom_ok.txt",
            L"--mode dayi --import-custom {in}\\custom_bad.txt", L"--mode dayi --import-custom {in}\\custom_bad.txt --json",
            L"--mode dayi --export-custom {dir}\\exp_dayi.txt",
            L"--mode array --import-custom {in}\\custom_bad.txt", L"--mode array --import-custom {in}\\custom_bad.txt --no-validate",
            L"--mode array --export-custom {dir}\\exp_array.txt",
            L"--mode phonetic --import-custom {in}\\custom_utf8.txt", L"--mode generic --load-main {in}\\table.cin",
            L"--mode generic --import-custom {in}\\custom_utf8.txt", L"--mode generic --import-custom {in}\\missing.txt",
            L"--mode generic --import-custom {in}\\custom_empty.txt", L"--mode generic --export-custom {dir}\\nofolder\\x.txt",
            L"--mode dayi --import-custom {in}\\custom_ok.txt --silent", L"--mode dayi --load-main {in}\\garbage.cin",
            L"--mode phonetic --load-phrase {in}\\table.cin", L"--mode array --load-array sp {in}\\table.cin",
            L"--mode array --load-array ext-b {in}\\table.cin", L"--mode array --load-array phrase {in}\\missing.cin",
            L"--mode array --load-array bogus {in}\\table.cin", L"--load-main {in}\\table.cin",
            L"--mode array --load-array array40 {in}\\table.cin", L"--mode array --get-all",
            L"--mode bogus --get-all", L"--mode", L"--set FontSize", L"--mode dayi --set", L"--frobnicate",
            L"--mode dayi --set FontFaceName Consolas --set CustomTablePriority 1", L"--mode dayi --get-all",
        };
    }

    struct Result {
        int rc;
        std::wstring text;
    };

    std::vector<std::wstring> RunSide(bool cpp, const std::wstring& dir, const std::wstring& in, const DimeConfigEnv& env)
    {
        RemoveTree(dir);
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring outPath = dir + L"_out.txt", errPath = dir + L"_err.txt";
        std::vector<std::wstring> log;
        for (const std::wstring& c : Commands()) {
            // A new process: initial values and CConfig statics.
            DimeSettings* fresh = dime_settings_new(nullptr, &env);
            DimeConfigValues v = {};
            dime_settings_get_values(fresh, &v);
            dime_settings_free(fresh);
            dime_settings_set_env(dime_settings_global(), &env);
            CConfig::ImportValues(&v);
            CConfig::SetReverseConversionDescriptionText(nullptr);
            Access::Reset();
            Global::imeMode = IME_MODE::IME_MODE_NONE;
            wcscpy_s(Global::testAppDataOverride, dir.c_str());

            std::wstring cmd = Replace(Replace(c, L"{in}", in), L"{dir}", dir);
            FILE* out = nullptr;
            FILE* err = nullptr;
            _wfopen_s(&out, outPath.c_str(), L"w");
            _wfopen_s(&err, errPath.c_str(), L"w");
            int rc = RunCLIForTest(cmd.c_str(), out, err, cpp);
            fclose(out);
            fclose(err);

            DimeConfigValues vals = {};
            CConfig::ExportValues(&vals);
            bool end = false;
            for (uint16_t& ch : vals.font_face_name) { if (end) ch = 0; else if (ch == 0) end = true; }
            std::vector<std::wstring> files;
            Files(dir, L"", files);
            std::string o = ReadBytes(outPath), e = ReadBytes(errPath);
            std::wstring s = c + L"\nrc " + std::to_wstring(rc)
                + L"\nout " + Replace(std::wstring(o.begin(), o.end()), std::wstring(dir.begin(), dir.end()), L"<dir>")
                + L"\nerr " + Replace(std::wstring(e.begin(), e.end()), std::wstring(dir.begin(), dir.end()), L"<dir>")
                + L"\nvalues " + Hex(std::string((const char*)&vals, sizeof(vals))) + L"\nfiles";
            for (const std::wstring& f : files) s += L"\n  " + f;
            log.push_back(s);
        }
        DeleteFileW(outPath.c_str());
        DeleteFileW(errPath.c_str());
        RemoveTree(dir);
        return log;
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(CliHostParityTest)
    {
        DimeConfigValues _savedValues = {};
        SettingsAppConfigAccess::Saved _savedStatics = {};
        WCHAR _savedAppData[MAX_PATH] = {};
        BOOL _saved1809 = FALSE;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            CConfig::ExportValues(&_savedValues);
            _savedStatics = SettingsAppConfigAccess::Save();
            wcscpy_s(_savedAppData, Global::testAppDataOverride);
            // The value DllMain / wWinMain set on Windows 10 1809+; tests that
            // run earlier may leave either value, and the defaults (colour
            // mode) recorded in the golden file depend on it.
            _saved1809 = Global::isWindows1809OrLater;
            Global::isWindows1809OrLater = TRUE;
        }

        TEST_METHOD_CLEANUP(Teardown)
        {
            CConfig::ImportValues(&_savedValues);
            CConfig::SetReverseConversionDescriptionText(nullptr);
            SettingsAppConfigAccess::Restore(_savedStatics);
            wcscpy_s(Global::testAppDataOverride, _savedAppData);
            Global::isWindows1809OrLater = _saved1809;
        }

        TEST_METHOD(Commands)
        {
            // The C++ side reads these itself, so both sides use the machine's
            // values; the golden file assumes the defaults (a white 3D
            // highlight colour and the test process's 1809 flag).
            DimeConfigEnv env = { GetSysColor(COLOR_3DHIGHLIGHT), Global::isWindows1809OrLater ? 1 : 0 };
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            std::wstring base = std::wstring(tmp) + L"DIMECliParity" + std::to_wstring(GetCurrentProcessId());
            RemoveTree(base);
            CreateDirectoryW(base.c_str(), nullptr);
            std::wstring in = base + L"\\in", pf = base + L"\\pf";
            MakeInputs(in);
            CreateDirectoryW(pf.c_str(), nullptr);
            wchar_t savedPf[MAX_PATH] = {};
            DWORD hadPf = GetEnvironmentVariableW(L"ProgramW6432", savedPf, MAX_PATH);
            SetEnvironmentVariableW(L"ProgramW6432", pf.c_str());
            IME_MODE savedMode = Global::imeMode;

            // The same folder name on both sides keeps the outputs comparable.
            std::wstring dir = base + L"\\app";
            std::vector<std::wstring> c = RunSide(true, dir, in, env);
            std::vector<std::wstring> r = RunSide(false, dir, in, env);

            SetEnvironmentVariableW(L"ProgramW6432", hadPf ? savedPf : nullptr);
            Global::imeMode = savedMode;
            RemoveTree(base);
            int withFiles = 0;
            Golden::Log golden;
            for (size_t i = 0; i < c.size(); i++) {
                if (c[i] != r[i]) {
                    // Only the lines that differ.
                    std::wstring diff;
                    size_t pc = 0, pr = 0;
                    while (pc < c[i].size() || pr < r[i].size()) {
                        size_t ec = c[i].find(L'\n', pc), er = r[i].find(L'\n', pr);
                        std::wstring lc = c[i].substr(pc, ec == std::wstring::npos ? std::wstring::npos : ec - pc);
                        std::wstring lr = r[i].substr(pr, er == std::wstring::npos ? std::wstring::npos : er - pr);
                        if (lc != lr) diff += L"C++:  " + lc.substr(0, 400) + L"\nRust: " + lr.substr(0, 400) + L"\n";
                        pc = ec == std::wstring::npos ? c[i].size() : ec + 1;
                        pr = er == std::wstring::npos ? r[i].size() : er + 1;
                    }
                    Assert::Fail((c[i].substr(0, c[i].find(L'\n')) + L"\n" + diff).c_str());
                }
                if (c[i].find(L"\n  DIME\\") != std::wstring::npos) withFiles++;
                // RunSide is the Rust host when its first argument is false.
                std::wstring result = Golden::Replace(r[i], base, L"<base>");
                Golden::Hash h;
                h.Add(result);
                golden.Add(result.substr(0, result.find(L'\n')) + L" " + h.Hex());
            }
            Golden::Check(L"CliHost", L"Commands", golden);
            Logger::WriteMessage((std::to_wstring(c.size()) + L" commands, " + std::to_wstring(withFiles) + L" with files\n").c_str());
        }
    };
}


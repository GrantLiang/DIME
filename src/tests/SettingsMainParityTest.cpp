/********************************************************************************
* SettingsMainParityTest: DIMESettings.exe built by cargo (phase 5b-15f,
* rust/crates/dime-settings-exe over dime-ffi settings_main.rs) vs what the
* C++ wWinMain did.
*
* - ConsoleWrite: the bytes the Rust executable writes for WriteWide (the
*   "C" locale, text mode: U+0000 ends a piece, units above U+00FF become
*   '?', '\n' becomes "\r\n") vs CLI.cpp's WriteWide on a CRT FILE* opened
*   in text mode, for random text (ASCII controls, Latin-1, CJK, surrogates,
*   pieces across the 4,096-byte stream buffer).
* - ProgramName: the Rust lpCmdLine vs the UCRT rule (copied below).
* - ExecutableCli: the real executable started with redirected stdout /
*   stderr (CreateProcess, STARTF_USESTDHANDLES) vs RunCLIForTest on the Rust
*   host into text-mode files: exit code and both outputs byte for byte.
*   Only commands that do not read %APPDATA% (the executable has no test
*   override), including made-up program names.
*
* The executable is rust\target\<triple>\release\DIMESettings.exe, built by
* this project's cargo step for the test platform.
*
* Golden files (src/tests/golden/SettingsMain): the Rust bytes per console
* case (hashed), the Rust lpCmdLine per program-name case, and the exit code
* and output hashes per executable command line.
*
* Source is ASCII only (the project compiles with code page 950).
********************************************************************************/
#include "pch.h"

#if defined(DIME_RUST_AVAILABLE) && defined(DIME_USE_RUST_CONFIG)

#include "CppUnitTest.h"
#include <Windows.h>
#include <clocale>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/CLI.h"
#include "dime_ffi.h"
#include "Golden.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    std::wstring TempDir()
    {
        WCHAR base[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, base);
        std::wstring dir = std::wstring(base) + L"DIMESettingsMainParity_" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }

    std::string ReadAll(const std::wstring& path)
    {
        std::string out;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return out;
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
        CloseHandle(h);
        return out;
    }

    std::wstring Hex(const std::string& s, size_t at)
    {
        WCHAR b[64];
        size_t from = at > 8 ? at - 8 : 0;
        std::wstring r;
        for (size_t i = from; i < s.size() && i < at + 8; i++) {
            swprintf_s(b, L"%02X ", (unsigned char)s[i]);
            r += b;
        }
        return r;
    }

    void AssertSameBytes(const std::string& want, const std::string& got, const std::wstring& what)
    {
        if (want == got) return;
        size_t i = 0;
        while (i < want.size() && i < got.size() && want[i] == got[i]) i++;
        std::wstring msg = what + L": sizes " + std::to_wstring(want.size()) + L"/" + std::to_wstring(got.size()) +
            L", first difference at " + std::to_wstring(i) + L" (C++ " + Hex(want, i) + L"| Rust " + Hex(got, i) + L")";
        Assert::Fail(msg.c_str());
    }

    // The process runs with the "C" locale, as DIMESettings.exe does (it
    // never calls setlocale); restore whatever another test left.
    struct CLocale
    {
        std::wstring saved;
        CLocale()
        {
            const wchar_t* cur = _wsetlocale(LC_ALL, nullptr);
            saved = cur ? cur : L"C";
            _wsetlocale(LC_ALL, L"C");
        }
        ~CLocale() { _wsetlocale(LC_ALL, saved.c_str()); }
    };

    // C++: WriteWide through a text-mode FILE*. Rust: the executable's
    // stream on a file handle.
    void WriteBoth(const std::wstring& dir, const std::vector<std::wstring>& calls, std::string& cpp, std::string& rust, bool buffered)
    {
        std::wstring pc = dir + L"\\cpp.bin", pr = dir + L"\\rust.bin";
        FILE* f = nullptr;
        Assert::AreEqual(0, (int)_wfopen_s(&f, pc.c_str(), L"w"));
        for (const auto& t : calls) WriteWideForTest(f, t.data(), t.size());
        fclose(f);
        HANDLE h = CreateFileW(pr.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(h != INVALID_HANDLE_VALUE);
        for (const auto& t : calls)
            dime_settings_console_write_for_test(h, reinterpret_cast<const uint16_t*>(t.data()), t.size(), buffered ? 1 : 0);
        CloseHandle(h);
        cpp = ReadAll(pc);
        rust = ReadAll(pr);
    }

    std::wstring RandomText(std::mt19937& rng, size_t len)
    {
        static const wchar_t pool[] = {
            L'a', L'Z', L'0', L' ', L'\n', L'\n', L'\r', L'\t', 0x1A, 0x7F, 0x00, 0x01,
            0x80, 0x85, 0xA0, 0xE9, 0xFF, 0x100, 0x3000, 0x4E2D, 0x6613, 0x20AC, 0xD83D, 0xDE00, 0xDC00, 0xFFFD, 0xFFFF,
        };
        std::wstring s;
        for (size_t i = 0; i < len; i++) {
            unsigned r = rng() % 100;
            if (r < 55) s += (wchar_t)(L'a' + rng() % 26);
            else if (r < 62) s += (wchar_t)(0x80 + rng() % 0x80);
            else if (r < 66) s += (wchar_t)(0x100 + rng() % 0xFF00);
            else s += pool[rng() % (sizeof(pool) / sizeof(pool[0]))];
        }
        // Mostly no NUL: a NUL ends the rest of its call, which would hide
        // everything after it.
        if (rng() % 4) for (auto& c : s) if (c == 0) c = L'.';
        return s;
    }

    // The UCRT's wWinMain lpCmdLine (exe_common.inl): skip the program name
    // (characters above space; a double quote toggles quoting), then blanks.
    std::wstring UcrtCmdLine(const std::wstring& full)
    {
        const wchar_t* p = full.c_str();
        bool quoted = false;
        while (*p > L' ' || (*p && quoted)) {
            if (*p == L'"') quoted = !quoted;
            ++p;
        }
        while (*p && *p <= L' ') ++p;
        return p;
    }

    std::wstring ExePath()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ExePath), &self);
        WCHAR path[MAX_PATH] = {};
        GetModuleFileNameW(self, path, MAX_PATH);
        // src\bin\<Platform>\<Configuration>\DIMETests.dll -> repository root
        std::wstring p = path;
        for (int i = 0; i < 5; i++) p = p.substr(0, p.find_last_of(L'\\'));
#ifdef _WIN64
        const wchar_t* triple = L"x86_64-pc-windows-msvc";
#else
        const wchar_t* triple = L"i686-pc-windows-msvc";
#endif
        return p + L"\\rust\\target\\" + triple + L"\\release\\DIMESettings.exe";
    }

    // Runs the executable with the full command line `full`; stdout and
    // stderr go to files. Returns the exit code.
    DWORD RunExe(const std::wstring& exe, const std::wstring& full, const std::wstring& outPath, const std::wstring& errPath)
    {
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE err = CreateFileW(errPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(out != INVALID_HANDLE_VALUE && err != INVALID_HANDLE_VALUE);
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nullptr;
        si.hStdOutput = out;
        si.hStdError = err;
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> cmd(full.begin(), full.end());
        cmd.push_back(0);
        BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(out);
        CloseHandle(err);
        Assert::IsTrue(ok != FALSE, (L"CreateProcess failed: " + exe).c_str());
        DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
        DWORD code = 0xDEAD;
        if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
        else TerminateProcess(pi.hProcess, 0xDEAD);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Assert::AreEqual((DWORD)WAIT_OBJECT_0, wait, (L"timed out: " + full).c_str());
        return code;
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsMainParityTest)
    {
    public:
        TEST_METHOD(ConsoleWrite)
        {
            CLocale c;
            std::wstring dir = TempDir();
            std::mt19937 rng(20261115);
            int cases = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"rounds", 20);
            for (int round = 0; round < 400; round++) {
                std::vector<std::wstring> calls;
                int n = 1 + (int)(rng() % 5);
                for (int i = 0; i < n; i++) {
                    size_t len = (round % 20 == 0) ? 3000 + rng() % 3000 : rng() % 200;
                    calls.push_back(RandomText(rng, len));
                }
                std::string cpp, rust;
                bool buffered = (round % 2) == 0;
                WriteBoth(dir, calls, cpp, rust, buffered);
                AssertSameBytes(cpp, rust, L"round " + std::to_wstring(round));
                ch.Add(rust.data(), rust.size());
                ch.Next();
                cases++;
            }
            // Fixed cases: the probe's strings and NULs.
            const std::vector<std::vector<std::wstring>> fixed = {
                { L"a\nb\r\n", L"x\u00e9y", L"p\u4e2dq\nr", std::wstring(L"s\xd800t"), L"u\u20acv", L"end\n" },
                { std::wstring(L"e\0f", 3), std::wstring(L"\0", 1), L"g" },
                { std::wstring(4096, L'x'), L"\n", std::wstring(4095, L'\n') },
                { L"" },
            };
            for (size_t i = 0; i < fixed.size(); i++) {
                std::string cpp, rust;
                WriteBoth(dir, fixed[i], cpp, rust, true);
                AssertSameBytes(cpp, rust, L"fixed " + std::to_wstring(i));
                Golden::Hash h;
                h.Add(rust.data(), rust.size());
                golden.Add(L"fixed " + std::to_wstring(i) + L": " + std::to_wstring(rust.size()) + L" bytes " + h.Hex());
            }
            ch.Finish();
            Golden::Check(L"SettingsMain", L"ConsoleWrite", golden.lines);
            DeleteFileW((dir + L"\\cpp.bin").c_str());
            DeleteFileW((dir + L"\\rust.bin").c_str());
            RemoveDirectoryW(dir.c_str());
            Logger::WriteMessage((L"ConsoleWrite: " + std::to_wstring(cases) + L" random cases\n").c_str());
        }

        TEST_METHOD(ProgramName)
        {
            const std::vector<std::wstring> lines = {
                L"DIMESettings.exe --help",
                L"\"C:\\Program Files\\DIME\\DIMESettings.exe\"  --mode dayi",
                L"a\"b c\"d \t x y",
                L"prog",
                L"\"unterminated rest of it",
                L"",
                L"  leading",
                L"p\x01q r",
                L"\"\" next",
                L"x\u3000y z",
            };
            Golden::Log golden;
            for (const auto& line : lines) {
                uint16_t out[512] = {};
                size_t n = dime_settings_skip_program_name_for_test(reinterpret_cast<const uint16_t*>(line.c_str()), out, 512);
                std::wstring got(reinterpret_cast<const wchar_t*>(out), n);
                Assert::AreEqual(UcrtCmdLine(line), got, line.c_str());
                golden.Add(L"[" + line + L"] -> [" + got + L"]");
            }
            Golden::Check(L"SettingsMain", L"ProgramName", golden.lines);
        }

        TEST_METHOD(ExecutableCli)
        {
            CLocale c;
            std::wstring exe = ExePath();
            Assert::IsTrue(GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES, (L"missing " + exe).c_str());
            std::wstring dir = TempDir();
            // Full command lines: the first token is the program name.
            const std::vector<std::wstring> lines = {
                L"DIMESettings.exe --help",
                L"DIMESettings.exe --help --silent",
                L"DIMESettings.exe --list-modes",
                L"DIMESettings.exe --list-modes --json",
                L"DIMESettings.exe --bogus \u4e2d\u00e9",
                L"DIMESettings.exe --get",
                L"DIMESettings.exe --mode",
                L"DIMESettings.exe --mode xyz --get Foo",
                L"DIMESettings.exe --set",
                L"DIMESettings.exe \"quoted arg\" --help",
                L"\"C:\\some where\\x.exe\"   --list-modes",
                L"odd\"name with\"space --bogus\tx",
                L"DIMESettings.exe --import-custom",
                L"DIMESettings.exe --list-keys",
                L"DIMESettings.exe --mode \u5927\u6613 --get-all",
                L"DIMESettings.exe --mode \u00e9x\u00ff --get-all",
                L"DIMESettings.exe --mode \u0080\u009f\u00a0 --list-keys",
            };
            Golden::Log golden;
            for (const auto& line : lines) {
                std::wstring cmd = UcrtCmdLine(line);
                std::wstring co = dir + L"\\cpp_out.txt", ce = dir + L"\\cpp_err.txt";
                FILE* fo = nullptr;
                FILE* fe = nullptr;
                Assert::AreEqual(0, (int)_wfopen_s(&fo, co.c_str(), L"w"));
                Assert::AreEqual(0, (int)_wfopen_s(&fe, ce.c_str(), L"w"));
                int want = RunCLIForTest(cmd.c_str(), fo, fe, false);
                fclose(fo);
                fclose(fe);
                std::wstring ro = dir + L"\\rust_out.txt", re = dir + L"\\rust_err.txt";
                DWORD got = RunExe(exe, line, ro, re);
                Assert::AreEqual((DWORD)want, got, (L"exit code: " + line).c_str());
                AssertSameBytes(ReadAll(co), ReadAll(ro), L"stdout of " + line);
                AssertSameBytes(ReadAll(ce), ReadAll(re), L"stderr of " + line);
                std::string out = ReadAll(ro), err = ReadAll(re);
                Golden::Hash ho, he;
                ho.Add(out.data(), out.size());
                he.Add(err.data(), err.size());
                golden.Add(L"[" + line + L"] exit " + std::to_wstring(got) + L" stdout " + std::to_wstring(out.size()) + L" "
                    + ho.Hex() + L" stderr " + std::to_wstring(err.size()) + L" " + he.Hex());
            }
            Golden::Check(L"SettingsMain", L"ExecutableCli", golden.lines);
            for (const wchar_t* n : { L"cpp_out.txt", L"cpp_err.txt", L"rust_out.txt", L"rust_err.txt" })
                DeleteFileW((dir + L"\\" + n).c_str());
            RemoveDirectoryW(dir.c_str());
        }
    };
}

#endif

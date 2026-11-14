/********************************************************************************
* CinConvertParityTest: CConfig::parseCINFile (C++, the CRT's text-mode wide
* file I/O and swscanf_s) vs dime_settings_parse_cin_file (Rust,
* dime-config cin_convert.rs, phase 5b-15b). The same source file goes
* through both, as a table (ccs=UTF-8) and as a custom table
* (ccs=UTF-16LE); the return value and the bytes written must match.
*
* Sources: random tables built from keys, separators and values at the
* parser's limits (64-unit keys and separators, 960-unit values, lines past
* 1023 units), section markers in any case, quotes and backslashes, lines
* without a key-value pair, NUL, Ctrl-Z, a lone CR, U+3000, CRLF / LF / CR
* line ends, invalid UTF-8, an odd last byte, in UTF-8 with and without a
* BOM and UTF-16LE with and without one; plus every installer/*.cin.
*
* Source is ASCII only (the project compiles with code page 950).
********************************************************************************/
#include "pch.h"

#ifdef DIME_RUST_AVAILABLE

#include "CppUnitTest.h"
#include <Windows.h>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../Config.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    std::wstring RepoRoot()
    {
        std::wstring p = __FILEW__;            // ...\src\tests\CinConvertParityTest.cpp
        for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
        return p + L"\\";
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

    std::string Utf8(const std::wstring& s)
    {
        if (s.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    std::string Utf16(const std::wstring& s)
    {
        return std::string((const char*)s.data(), s.size() * sizeof(wchar_t));
    }

    std::wstring Rep(wchar_t c, int n) { return std::wstring(n, c); }

    struct Gen {
        std::mt19937 rng;
        explicit Gen(unsigned seed) : rng(seed) {}
        int R(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
        template <class T> const T& Pick(const std::vector<T>& v) { return v[R(0, (int)v.size() - 1)]; }

        std::wstring Key()
        {
            static const std::vector<std::wstring> keys = {
                L"a", L"ab", L"zz9", std::wstring(L"\x4E2D\x6587"), L"x\"y", L"p\\q", L"k k", L"%chardef", L"%CHARDEF",
                L"%ChArDeF", L"%keyname", L"%KEYNAME", L"%encoding", L"%Encoding", L"%ename", L"%selkey", L"\"q\"",
                L"\"", L"\"\"", std::wstring(1, (wchar_t)0x3000), L"a\x3000" L"b", L"\\", L"%gen_inp",
            };
            switch (R(0, 9)) {
            case 0: return Rep(L'k', R(60, 66));
            case 1: return Rep(L'\\', R(28, 34));
            case 2: return Rep(L'"', R(28, 34));
            default: return Pick(keys);
            }
        }

        std::wstring Sep()
        {
            static const std::vector<std::wstring> seps = { L" ", L"\t", L"  ", L" \t ", L"\t\t", L"\t " };
            switch (R(0, 9)) {
            case 0: return Rep(L' ', R(61, 66));
            case 1: return Rep(L'\t', R(61, 66));
            default: return Pick(seps);
            }
        }

        std::wstring Value()
        {
            static const std::vector<std::wstring> values = {
                L"v", std::wstring(1, (wchar_t)0x4E2D), L"a b", L"\"x\"", L"v\\w", L"begin", L"BEGIN", L"Begin", L"end",
                L"End", L"END", L"1 2 3", std::wstring(1, (wchar_t)0x3000) + L"x", L"\"", L"x\"", L"\"x", L"UTF-8",
            };
            switch (R(0, 9)) {
            case 0: return Rep((wchar_t)0x4E2D, R(956, 962));
            case 1: return Rep(L'\\', R(470, 482));
            default: return Pick(values);
            }
        }

        std::wstring Line()
        {
            switch (R(0, 19)) {
            case 0: return L"";
            case 1: return Pick(std::vector<std::wstring>{ L" ", L"\t", L"  \t" });
            case 2: return Key();
            case 3: return Sep() + Key() + Sep() + Value();
            case 4: return Key() + Sep() + Value() + Sep() + Value();
            case 5: { std::wstring s; int n = R(1000, 1100); for (int i = 0; i < n; i++) s += (wchar_t)(R(0, 3) ? L'a' + R(0, 25) : L' '); return s; }
            case 6: return Key() + Sep() + Value() + std::wstring(1, L'\0') + L"tail";
            case 7: return Key() + Sep() + Value() + std::wstring(1, (wchar_t)0x1A) + L"z";
            case 8: return Key() + L"\r" + Value();
            case 9: return L"a\tb\tc";
            case 10: return std::wstring(L"%chardef") + Sep() + Pick(std::vector<std::wstring>{ L"begin", L"end", L"BEGIN", L"End" });
            default: return Key() + Sep() + Value();
            }
        }

        std::wstring Text()
        {
            std::wstring s;
            int n = R(1, 14);
            for (int i = 0; i < n; i++) {
                s += Line();
                if (i + 1 < n || R(0, 1)) s += Pick(std::vector<std::wstring>{ L"\n", L"\n", L"\r\n", L"\r\n", L"\r" });
            }
            return s;
        }

        // The file bytes for `text` in a random encoding.
        std::string Bytes(const std::wstring& text, std::wstring& how)
        {
            std::string b;
            switch (R(0, 3)) {
            case 0: how = L"utf8"; b = Utf8(text); break;
            case 1: how = L"utf8-bom"; b = "\xEF\xBB\xBF" + Utf8(text); break;
            case 2: how = L"utf16-bom"; b = std::string("\xFF\xFE", 2) + Utf16(text); break;
            default: how = L"utf16"; b = Utf16(text); break;
            }
            int damage = R(0, 9);
            if (damage == 0 && how.rfind(L"utf8", 0) == 0 && !b.empty()) {
                static const char* bad[] = { "\xFF", "\xC3", "\xE4\xB8", "\x80", "\xED\xA0\x80" };
                b.insert((size_t)R(0, (int)b.size()), bad[R(0, 4)]);
                how += L"+bad";
            } else if (damage == 1) {
                b += '\x41';
                how += L"+odd";
            }
            return b;
        }
    };

    struct Dirs {
        std::wstring base, src, outC, outR;
        Dirs()
        {
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            base = std::wstring(tmp) + L"DIMECinParity" + std::to_wstring(GetCurrentProcessId());
            CreateDirectoryW(base.c_str(), nullptr);
            src = base + L"\\src.txt";
            outC = base + L"\\out_c.cin";
            outR = base + L"\\out_r.cin";
        }
        ~Dirs()
        {
            DeleteFileW(src.c_str());
            DeleteFileW(outC.c_str());
            DeleteFileW(outR.c_str());
            RemoveDirectoryW(base.c_str());
        }
    };

    // Both converters on `srcPath`; fails with the first differing byte.
    void Compare(const Dirs& d, const std::wstring& srcPath, bool custom, const std::wstring& what)
    {
        DeleteFileW(d.outC.c_str());
        DeleteFileW(d.outR.c_str());
        BOOL rc = CConfig::parseCINFile(srcPath.c_str(), d.outC.c_str(), custom ? TRUE : FALSE, TRUE);
        int rr = dime_settings_parse_cin_file((const uint16_t*)srcPath.c_str(), (const uint16_t*)d.outR.c_str(), custom ? 1 : 0, 1);
        std::wstring w = what + (custom ? L" (custom)" : L" (table)");
        Assert::AreEqual(rc ? 1 : 0, rr, (w + L": result").c_str());
        std::string a = ReadBytes(d.outC), b = ReadBytes(d.outR);
        if (a == b) return;
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) i++;
        auto around = [&](const std::string& s) {
            std::wstring out;
            size_t from = i > 40 ? (i - 40) & ~(size_t)1 : 0;
            for (size_t k = from; k + 1 < s.size() && k < from + 120; k += 2) {
                wchar_t c = (wchar_t)((unsigned char)s[k] | ((unsigned char)s[k + 1] << 8));
                wchar_t buf[8];
                swprintf_s(buf, c >= 32 && c < 127 ? L"%c" : L"<%X>", c);
                out += buf;
            }
            return out;
        };
        // Keep the source for a look.
        std::wstring keep = d.base + L"_fail_src.bin";
        WriteBytes(keep, ReadBytes(srcPath));
        Assert::Fail((w + L": output differs at byte " + std::to_wstring(i) + L" (sizes " + std::to_wstring(a.size()) + L" / "
            + std::to_wstring(b.size()) + L"), source kept in " + keep + L"\nC++:  " + around(a) + L"\nRust: " + around(b)).c_str());
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(CinConvertParityTest)
    {
    public:
        TEST_METHOD(RandomTables)
        {
            Dirs d;
            int cases = 0;
            for (unsigned seed = 1; seed <= 1500; seed++) {
                Gen g(seed);
                std::wstring how;
                std::string bytes = g.Bytes(g.Text(), how);
                WriteBytes(d.src, bytes);
                std::wstring what = L"seed " + std::to_wstring(seed) + L" " + how;
                Compare(d, d.src, false, what);
                Compare(d, d.src, true, what);
                cases++;
            }
            Logger::WriteMessage((std::to_wstring(cases) + L" random sources\n").c_str());
        }

        TEST_METHOD(InstallerTables)
        {
            Dirs d;
            WIN32_FIND_DATAW fd;
            std::wstring dir = RepoRoot() + L"installer\\";
            HANDLE h = FindFirstFileW((dir + L"*.cin").c_str(), &fd);
            Assert::IsTrue(h != INVALID_HANDLE_VALUE, L"installer tables");
            int n = 0;
            do {
                Compare(d, dir + fd.cFileName, false, fd.cFileName);
                n++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            Logger::WriteMessage((std::to_wstring(n) + L" installer tables\n").c_str());
            Assert::IsTrue(n >= 8, std::to_wstring(n).c_str());
        }

        // The UCRT reads UTF-8 2048 bytes at a time: sequences split across
        // pieces, invalid bytes and unfinished sequences at piece ends.
        TEST_METHOD(Utf8Pieces)
        {
            Dirs d;
            const std::string tails[] = { std::string("\xE4\xB8\xAD"), std::string("\xC3\xA9"), std::string("\x80"),
                std::string("\xFF"), std::string("\xC3"), std::string("\xE4\xB8"), std::string("\xF0\x9F\x98\x80"),
                std::string("\xC3\xA9\x80"), std::string("\x80\x80\x80\x80\x80"), std::string("\xF8"), std::string("\n") };
            const int bases[] = { 2040, 2044, 2045, 2046, 2047, 2048, 4092, 4094, 4095, 4096, 6140, 6143 };
            int n = 0;
            for (int base : bases) {
                for (const std::string& a : tails) {
                    for (const std::string& b : tails) {
                        std::string text;
                        for (int i = 0; i < base; i++) text += (i % 50 == 49) ? '\n' : (i % 7 == 0 ? ' ' : (char)('a' + i % 26));
                        text += a + "x y\n" + b + "zz\n" + a;
                        WriteBytes(d.src, text);
                        Compare(d, d.src, false, L"base " + std::to_wstring(base));
                        WriteBytes(d.src, "\xEF\xBB\xBF" + text);
                        Compare(d, d.src, false, L"bom base " + std::to_wstring(base));
                        n++;
                    }
                }
            }
            Logger::WriteMessage((std::to_wstring(n) + L" piece cases\n").c_str());
        }
        TEST_METHOD(MissingSource)
        {
            Dirs d;
            Compare(d, d.base + L"\\no_such_file.txt", false, L"missing");
            Compare(d, d.base + L"\\no_such_file.txt", true, L"missing");
            // Unwritable destination: a directory.
            WriteBytes(d.src, "a b\n");
            BOOL rc = CConfig::parseCINFile(d.src.c_str(), d.base.c_str(), FALSE, TRUE);
            int rr = dime_settings_parse_cin_file((const uint16_t*)d.src.c_str(), (const uint16_t*)d.base.c_str(), 0, 1);
            Assert::AreEqual(rc ? 1 : 0, rr, L"destination is a folder");
        }
    };
}

#endif

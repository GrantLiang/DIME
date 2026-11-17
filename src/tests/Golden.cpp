#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <algorithm>
#include <fstream>
#include "Golden.h"
#include "dime_ffi.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Golden {

namespace {

std::string ToUtf8(const std::wstring& s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::wstring Dir(const std::wstring& group) { return RepoRoot() + L"src\\tests\\golden\\" + group; }
std::wstring PathOf(const std::wstring& group, const std::wstring& name) { return Dir(group) + L"\\" + name + L".txt"; }

// A line may not hold '\n' or '\r' (the file is line based); escape them.
std::wstring Escape(const std::wstring& line)
{
    std::wstring out;
    for (wchar_t c : line) {
        if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else out += c;
    }
    return out;
}

void Record(const std::wstring& group, const std::wstring& name, const std::vector<std::wstring>& lines)
{
    CreateDirectoryW((RepoRoot() + L"src\\tests\\golden").c_str(), nullptr);
    CreateDirectoryW(Dir(group).c_str(), nullptr);
    std::wstring path = PathOf(group, name);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    for (const auto& l : lines) f << ToUtf8(Escape(l)) << "\n";
    Logger::WriteMessage((L"recorded " + path + L"\n").c_str());
}

void CompareFile(const std::wstring& group, const std::wstring& name, const std::vector<std::wstring>& lines,
    const std::wstring& who, const std::vector<std::wstring>* details = nullptr)
{
    std::wstring path = PathOf(group, name);
    std::ifstream f(path, std::ios::binary);
    Assert::IsTrue(f.good(), (L"missing golden file " + path + L" (run with DIME_GOLDEN_RECORD=1)").c_str());
    std::vector<std::wstring> expected;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("# ", 0) == 0) continue;
        expected.push_back(FromUtf8(line));
    }
    size_t n = (std::max)(expected.size(), lines.size());
    for (size_t i = 0; i < n; i++) {
        std::wstring e = i < expected.size() ? expected[i] : L"<end of file>";
        std::wstring a = i < lines.size() ? Escape(lines[i]) : L"<end of output>";
        if (e != a) {
            std::wstring before = i > 0 && i - 1 < expected.size() ? expected[i - 1] : L"";
            // The test framework cuts long messages short: the output behind
            // the line goes to the test's standard output, in pieces.
            if (details && i < details->size() && !(*details)[i].empty()) {
                const std::wstring& d = (*details)[i];
                Logger::WriteMessage((L"actual output behind " + group + L"/" + name + L" line " + std::to_wstring(i + 1)
                    + L":\n").c_str());
                for (size_t p = 0; p < d.size(); p += 1000)
                    Logger::WriteMessage(d.substr(p, 1000).c_str());
            }
            Assert::Fail((group + L"/" + name + (who.empty() ? L"" : L" (" + who + L")") + L" line "
                + std::to_wstring(i + 1) + L"\n  expected: " + e + L"\n    actual: " + a + L"\n    before: " + before
                + L"\n  (the output behind this line is in the test's standard output)").c_str());
        }
    }
}

} // namespace

std::wstring RepoRoot()
{
    std::wstring p = __FILEW__;  // ...\src\tests\Golden.cpp
    for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of(L'\\'));
    return p + L"\\";
}

bool Recording()
{
    wchar_t v[8] = {};
    return GetEnvironmentVariableW(L"DIME_GOLDEN_RECORD", v, 8) && v[0] == L'1';
}

const wchar_t* PlatformSuffix()
{
    return sizeof(void*) == 8 ? L"-x64" : L"-x86";
}

void Check(const std::wstring& group, const std::wstring& name, const std::vector<std::wstring>& lines)
{
    if (Recording()) Record(group, name, lines);
    else CompareFile(group, name, lines, L"");
}

void Check(const std::wstring& group, const std::wstring& name, const Log& log)
{
    if (Recording()) Record(group, name, log.lines);
    else CompareFile(group, name, log.lines, L"", &log.details);
}

void Compare(const std::wstring& group, const std::wstring& name, const std::vector<std::wstring>& lines,
    const std::wstring& who)
{
    CompareFile(group, name, lines, who);
}

void Log::AddText(const std::wstring& text)
{
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find(L'\n', start);
        if (nl == std::wstring::npos) { Add(text.substr(start)); break; }
        Add(text.substr(start, nl - start));
        start = nl + 1;
    }
}

int Pick(std::mt19937& rng, int lo, int hi)
{
    uint64_t span = (uint64_t)((int64_t)hi - lo + 1);
    return (int)(lo + (int64_t)(rng() % span));
}

void Hash::Add(const void* bytes, size_t size)
{
    const unsigned char* p = (const unsigned char*)bytes;
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

void Hash::Add(const std::wstring& line)
{
    Add(line.data(), line.size() * sizeof(wchar_t));
    const wchar_t nl = L'\n';
    Add(&nl, sizeof(nl));
}

std::wstring Hash::Hex() const
{
    wchar_t buf[17] = {};
    swprintf_s(buf, L"%016llx", (unsigned long long)h);
    return buf;
}

void Chunks::Add(const std::wstring& line)
{
    hash.Add(line);
    if (detail.size() < 200000) detail += line + L"\n";
}

void Chunks::Add(const void* bytes, size_t n)
{
    hash.Add(bytes, n);
    if (detail.size() < 200000) {
        static const wchar_t* d = L"0123456789abcdef";
        detail += L"[" + std::to_wstring(n) + L" bytes] ";
        const unsigned char* p = (const unsigned char*)bytes;
        for (size_t i = 0; i < n && i < 256; i++) { detail += d[p[i] >> 4]; detail += d[p[i] & 15]; }
        detail += L"\n";
    }
}

void Chunks::Next()
{
    items++;
    if (items - first == size) {
        log.Add(label + L" " + std::to_wstring(first) + L".." + std::to_wstring(items - 1) + L" " + hash.Hex(), detail);
        hash = Hash();
        detail.clear();
        first = items;
    }
}

void Chunks::Finish()
{
    if (items > first)
        log.Add(label + L" " + std::to_wstring(first) + L".." + std::to_wstring(items - 1) + L" " + hash.Hex(), detail);
    hash = Hash();
    detail.clear();
    first = items;
}

GdiRecording::GdiRecording(bool synthetic)
{
    dime_settings_gdi_record_start(synthetic ? 1 : 0);
}

GdiRecording::~GdiRecording()
{
    if (!taken) dime_settings_gdi_record_take(nullptr, 0);
}

std::vector<std::wstring> GdiRecording::Take()
{
    taken = true;
    size_t n = dime_settings_gdi_record_take(nullptr, 0);
    std::vector<uint16_t> buf(n + 1);
    dime_settings_gdi_record_take(buf.data(), buf.size());
    std::wstring text((const wchar_t*)buf.data(), n);
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find(L'\n', start);
        if (nl == std::wstring::npos) nl = text.size();
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

std::wstring Replace(std::wstring s, const std::wstring& from, const std::wstring& to)
{
    if (from.empty()) return s;
    for (size_t p = s.find(from); p != std::wstring::npos; p = s.find(from, p + to.size()))
        s.replace(p, from.size(), to);
    return s;
}

std::wstring HidePointers(const std::wstring& s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == L'0' && i + 2 < s.size() && (s[i + 1] == L'x' || s[i + 1] == L'X') && iswxdigit(s[i + 2])) {
            size_t j = i + 2;
            while (j < s.size() && iswxdigit(s[j])) j++;
            out += L"<ptr>";
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

} // namespace Golden

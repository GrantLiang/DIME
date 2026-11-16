// Golden.h — golden files for tests whose C++ reference implementation is
// being deleted (phase 6 of docs/RUST_MIGRATION_PLAN.md).
//
// A golden file is src/tests/golden/<group>/<name>.txt: UTF-8, one line per
// entry, lines starting with "# " are comments. It holds what the code under
// test produced when it was still checked against the C++ implementation
// (the parity tests proved both sides equal), so comparing against it keeps
// that protection after the C++ side is gone.
//
// Set DIME_GOLDEN_RECORD=1 to (re)write the files from the current output,
// then review the diff of src/tests/golden. Without it, a missing file or
// any different line fails the test.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace Golden {

// The repository root, with a trailing backslash.
std::wstring RepoRoot();

// DIME_GOLDEN_RECORD=1.
bool Recording();

// "-x64" or "-x86": for golden files whose content depends on the pointer
// size (WPARAM / LPARAM values, structure sizes). Record on both platforms.
const wchar_t* PlatformSuffix();

// Records `lines` as <group>/<name> when recording; otherwise compares them
// with the file and fails at the first difference.
void Check(const std::wstring& group, const std::wstring& name, const std::vector<std::wstring>& lines);
class Log;
// The same for a Log; a failure also shows the details behind the line (the
// output a Chunks hash stands for), to compare with a local run.
void Check(const std::wstring& group, const std::wstring& name, const Log& log);

// Compares only (never records): for a second implementation that must
// produce the recorded output too, e.g. the C++ side while it still exists.
// `who` names it in the failure message.
void Compare(const std::wstring& group, const std::wstring& name, const std::vector<std::wstring>& lines,
    const std::wstring& who);

// Collects lines for one golden file.
class Log {
public:
    void Add(const std::wstring& line, const std::wstring& detail = std::wstring())
    {
        lines.push_back(line);
        details.push_back(detail);
    }
    // Several lines, split at '\n' (a trailing '\n' adds no empty line).
    void AddText(const std::wstring& text);
    std::vector<std::wstring> lines;
    std::vector<std::wstring> details;   // per line; shown when it differs
};

// A value in [lo, hi] from the raw output of `rng`. std::mt19937's sequence
// is fixed by the standard but std::uniform_int_distribution is not, so
// inputs behind a golden file must come from here to stay the same on every
// toolchain.
int Pick(std::mt19937& rng, int lo, int hi);

// FNV-1a over the UTF-16 text of each added line: one golden line can stand
// for a long run of output (the file names the run; record with the details
// in a local diff when one changes).
class Hash {
public:
    void Add(const std::wstring& line);
    void Add(const void* bytes, size_t size);
    std::wstring Hex() const;
private:
    uint64_t h = 1469598103934665603ULL;
};

// Hashes items in chunks: every `size` items (and the last, shorter chunk)
// become one line "<label> <first>..<last> <hash>" of `log`.
class Chunks {
public:
    Chunks(Log& log, const std::wstring& label, int size) : log(log), label(label), size(size) {}
    void Add(const std::wstring& line);
    void Add(const void* bytes, size_t n);
    // Ends one item.
    void Next();
    // Writes the last, shorter chunk.
    void Finish();
private:
    Log& log;
    std::wstring label;
    int size;
    int items = 0, first = 0;
    Hash hash;
    std::wstring detail;
};

// `s` with every occurrence of `from` replaced by `to`.
std::wstring Replace(std::wstring s, const std::wstring& from, const std::wstring& to);

// `s` with each "0x" followed by hex digits replaced by "<ptr>" (window
// handles and pointers differ from run to run).
std::wstring HidePointers(const std::wstring& s);

} // namespace Golden

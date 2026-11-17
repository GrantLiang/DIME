// RustSentenceBuffer.h - test-only: the CSentenceBuffer API over dime-core's
// sentence buffer (dime_sentence_buffer_*). The product's sentence mode is the
// Rust session's (dime-core::session); the C++ CSentenceBuffer was removed in
// phase 6c, and the sentence tests drive the Rust buffer through this class.
#pragma once

#include <string>
#include <vector>
#include "../SentenceBuffer.h"
#include "dime_ffi.h"

struct CSentencePosition
{
    std::wstring code;                     // keystrokes typed for this character
    std::vector<std::wstring> candidates;  // in table order, never empty
    int pinned = -1;                       // candidate fixed by the user, or -1
    int chosen = 0;                        // candidate picked by the conversion
};

class CSentenceBuffer
{
public:
    // Longest sentence kept in the composition (dime-core: MAX_POSITIONS).
    static const size_t MaxPositions = 40;

    CSentenceBuffer() : _b(dime_sentence_buffer_new()) {}
    ~CSentenceBuffer() { dime_sentence_buffer_free(_b); }
    CSentenceBuffer(const CSentenceBuffer&) = delete;
    CSentenceBuffer& operator=(const CSentenceBuffer&) = delete;

    bool IsEmpty() const { return Count() == 0; }
    size_t Count() const { return dime_sentence_buffer_count(_b); }
    // A copy of position i (valid until the next At of the same position).
    const CSentencePosition& At(size_t i) const
    {
        if (_at.size() <= i) _at.resize(i + 1);
        CSentencePosition& p = _at[i];
        size_t len = 0;
        const uint16_t* code = dime_sentence_buffer_position_code(_b, i, &len);
        p.code.assign(code ? reinterpret_cast<const wchar_t*>(code) : L"", code ? len : 0);
        p.candidates.clear();
        for (size_t k = 0; k < dime_sentence_buffer_candidate_count(_b, i); k++)
        {
            const uint16_t* c = dime_sentence_buffer_candidate(_b, i, k, &len);
            p.candidates.emplace_back(c ? reinterpret_cast<const wchar_t*>(c) : L"", c ? len : 0);
        }
        p.pinned = dime_sentence_buffer_pinned(_b, i);
        p.chosen = dime_sentence_buffer_chosen(_b, i);
        return p;
    }

    size_t Cursor() const { return dime_sentence_buffer_cursor(_b); }
    void MoveLeft() { dime_sentence_buffer_move(_b, 0); }
    void MoveRight() { dime_sentence_buffer_move(_b, 1); }
    void Home() { dime_sentence_buffer_move(_b, 2); }
    void End() { dime_sentence_buffer_move(_b, 3); }

    bool Insert(const std::wstring& code, const std::vector<std::wstring>& candidates, int pinned)
    {
        std::vector<const uint16_t*> ptrs;
        std::vector<size_t> lens;
        for (const std::wstring& c : candidates)
        {
            ptrs.push_back(reinterpret_cast<const uint16_t*>(c.c_str()));
            lens.push_back(c.size());
        }
        return dime_sentence_buffer_insert(_b, reinterpret_cast<const uint16_t*>(code.c_str()), code.size(),
            ptrs.data(), lens.data(), candidates.size(), pinned) != 0;
    }
    bool RemoveBeforeCursor() { return dime_sentence_buffer_remove_before_cursor(_b) != 0; }
    bool RemoveAtCursor() { return dime_sentence_buffer_remove_at_cursor(_b) != 0; }
    void Clear() { dime_sentence_buffer_clear(_b); }

    size_t EditTarget() const { return dime_sentence_buffer_edit_target(_b); }
    bool Pin(size_t pos, int index) { return dime_sentence_buffer_pin(_b, pos, index) != 0; }

    void Convert(const CSentenceLexicon* pLexicon, const CSentenceUserModel* pUser = nullptr)
    {
        dime_sentence_buffer_convert(_b, pLexicon ? pLexicon->Handle() : nullptr, pUser ? pUser->Handle() : nullptr);
    }
    // CSentenceUserModel::Learn(sentence): learn from the pinned positions.
    bool LearnInto(CSentenceUserModel& user) { return dime_sentence_buffer_learn(_b, user.Handle()) != 0; }

    std::wstring Text() const { return Take(dime_sentence_buffer_text(_b)); }
    size_t TextLength(size_t n) const { return dime_sentence_buffer_text_length(_b, n); }
    std::wstring Display(const std::wstring& reading) const
    {
        return Take(dime_sentence_buffer_display(_b, reinterpret_cast<const uint16_t*>(reading.c_str()), reading.size()));
    }
    size_t DisplayCaret(const std::wstring& reading) const
    {
        return dime_sentence_buffer_display_caret(_b, reinterpret_cast<const uint16_t*>(reading.c_str()), reading.size());
    }

private:
    static std::wstring Take(DimeBuffer* b)
    {
        size_t len = 0;
        const uint16_t* p = b ? dime_buffer_data(b, &len) : nullptr;
        std::wstring s(p ? reinterpret_cast<const wchar_t*>(p) : L"", p ? len : 0);
        if (b) dime_buffer_free(b);
        return s;
    }

    DimeSentenceBuffer* _b;
    mutable std::vector<CSentencePosition> _at;
};

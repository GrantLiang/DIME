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

// Sentence mode ("continuous input"): the user types one code per character
// without choosing candidates; each typed character becomes a position that
// keeps its whole candidate list, and the conversion (dime_core::sentence in
// Rust) picks the candidates that read best together.  Positions the user
// fixes by hand are pinned, and on commit CSentenceUserModel learns from them
// (preferred character per code, and the pairs it forms with its neighbours).
//
// The sentence itself (positions, cursor, conversion, learning) is the Rust
// session's (dime-core::session / sentence_buffer); the C++ CSentenceBuffer
// was removed in phase 6c. This file keeps the lexicon and the user model
// CDIME loads, saves and hands to the session. Free of TSF so it can be unit
// tested.

#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct DimeLexicon;
struct DimeUserModel;

// Word list + character frequencies used by the conversion.  Without the Rust
// library (DIME_RUST_AVAILABLE undefined) it is an empty shell and the
// conversion falls back to the first (or pinned) candidate of every position.
class CSentenceLexicon
{
public:
	CSentenceLexicon();
	~CSentenceLexicon();
	CSentenceLexicon(const CSentenceLexicon&) = delete;
	CSentenceLexicon& operator=(const CSentenceLexicon&) = delete;

	static bool IsSupported();

	// Multi-character values of a .cin table become words (e.g. Array-Phrase.cin).
	size_t LoadCinValues(_In_reads_(len) const WCHAR* buf, size_t len);
	// "text<TAB>count" lines (e.g. TCFreq.cin).
	size_t LoadFreqTable(_In_reads_(len) const WCHAR* buf, size_t len);
	bool AddWord(const std::wstring& word, UINT freq = 0);
	size_t WordCount() const;
	size_t CharCount() const;

	DimeLexicon* Handle() const { return _pLexicon; }

private:
	DimeLexicon* _pLexicon;
};

// What the user taught sentence mode.  Persisted as text (UTF-16 with BOM);
// several processes share one file, so a save re-reads the file and adds only
// this process's new data (kept separately as "pending").
class CSentenceUserModel
{
public:
	CSentenceUserModel();
	~CSentenceUserModel();
	CSentenceUserModel(const CSentenceUserModel&) = delete;
	CSentenceUserModel& operator=(const CSentenceUserModel&) = delete;

	size_t Import(_In_reads_(len) const WCHAR* buf, size_t len);
	std::wstring Export() const;
	void Merge(const CSentenceUserModel& other);
	void Clear();
	size_t WordCount() const;
	size_t ChoiceCount() const;

	// Add the entries of a file written by SaveFile (missing file: 0).
	size_t LoadFile(_In_z_ LPCWSTR path);
	// Write Export() to path (via a temporary file).  FALSE on failure.
	BOOL SaveFile(_In_z_ LPCWSTR path) const;

	DimeUserModel* Handle() const { return _pModel; }

private:
	DimeUserModel* _pModel;
};

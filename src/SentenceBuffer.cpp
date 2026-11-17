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

#include "SentenceBuffer.h"

#include "dime_ffi.h"

namespace
{
	static_assert(sizeof(WCHAR) == sizeof(uint16_t), "WCHAR must be UTF-16");
	inline const uint16_t* U16(const WCHAR* p) { return reinterpret_cast<const uint16_t*>(p); }

}

//////////////////////////////////////////////////////////////////////
// CSentenceLexicon
//////////////////////////////////////////////////////////////////////

CSentenceLexicon::CSentenceLexicon() : _pLexicon(nullptr)
{
	_pLexicon = dime_lexicon_new();
}

CSentenceLexicon::~CSentenceLexicon()
{
	dime_lexicon_free(_pLexicon);
	_pLexicon = nullptr;
}

bool CSentenceLexicon::IsSupported()
{
	return true;
}

size_t CSentenceLexicon::LoadCinValues(_In_reads_(len) const WCHAR* buf, size_t len)
{
	if (_pLexicon && buf) return dime_lexicon_load_cin_values(_pLexicon, U16(buf), len);
	return 0;
}

size_t CSentenceLexicon::LoadFreqTable(_In_reads_(len) const WCHAR* buf, size_t len)
{
	if (_pLexicon && buf) return dime_lexicon_load_freq_table(_pLexicon, U16(buf), len);
	return 0;
}

bool CSentenceLexicon::AddWord(const std::wstring& word, UINT freq)
{
	if (_pLexicon) return dime_lexicon_add_word(_pLexicon, U16(word.c_str()), word.length(), freq) != 0;
	return false;
}

size_t CSentenceLexicon::WordCount() const
{
	return dime_lexicon_word_count(_pLexicon);
}

size_t CSentenceLexicon::CharCount() const
{
	return dime_lexicon_char_count(_pLexicon);
}

//////////////////////////////////////////////////////////////////////
// CSentenceUserModel
//////////////////////////////////////////////////////////////////////

CSentenceUserModel::CSentenceUserModel() : _pModel(nullptr)
{
	_pModel = dime_user_model_new();
}

CSentenceUserModel::~CSentenceUserModel()
{
	dime_user_model_free(_pModel);
	_pModel = nullptr;
}

size_t CSentenceUserModel::Import(_In_reads_(len) const WCHAR* buf, size_t len)
{
	if (_pModel && buf) return dime_user_model_import(_pModel, U16(buf), len);
	return 0;
}

std::wstring CSentenceUserModel::Export() const
{
	std::wstring text;
	DimeBuffer* pBuffer = dime_user_model_export(_pModel);
	if (pBuffer)
	{
		size_t len = 0;
		const uint16_t* p = dime_buffer_data(pBuffer, &len);
		if (p) text.assign(reinterpret_cast<const WCHAR*>(p), len);
		dime_buffer_free(pBuffer);
	}
	return text;
}

void CSentenceUserModel::Merge(const CSentenceUserModel& other)
{
	dime_user_model_merge(_pModel, other._pModel);
}

void CSentenceUserModel::Clear()
{
	dime_user_model_clear(_pModel);
}

size_t CSentenceUserModel::WordCount() const
{
	return dime_user_model_word_count(_pModel);
}

size_t CSentenceUserModel::ChoiceCount() const
{
	return dime_user_model_choice_count(_pModel);
}

size_t CSentenceUserModel::LoadFile(_In_z_ LPCWSTR path)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return 0;

	size_t imported = 0;
	LARGE_INTEGER size = {};
	if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 64 * 1024 * 1024)
	{
		std::vector<WCHAR> data((size_t)size.QuadPart / sizeof(WCHAR) + 1, 0);
		DWORD read = 0;
		if (ReadFile(h, data.data(), (DWORD)size.QuadPart, &read, nullptr))
		{
			size_t len = read / sizeof(WCHAR);
			const WCHAR* p = data.data();
			if (len && *p == 0xFEFF) { p++; len--; }
			imported = Import(p, len);
		}
	}
	CloseHandle(h);
	return imported;
}

BOOL CSentenceUserModel::SaveFile(_In_z_ LPCWSTR path) const
{
	std::wstring text = Export();
	if (text.empty()) return FALSE;

	std::wstring temp = std::wstring(path) + L".tmp";
	HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return FALSE;

	const WCHAR bom = 0xFEFF;
	DWORD written = 0;
	DWORD bytes = (DWORD)(text.length() * sizeof(WCHAR));
	BOOL ok = WriteFile(h, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom) &&
		WriteFile(h, text.data(), bytes, &written, nullptr) && written == bytes;
	CloseHandle(h);

	if (ok)
		ok = MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	if (!ok)
		DeleteFileW(temp.c_str());
	return ok;
}

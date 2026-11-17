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

// Sentence mode ("continuous input") for CDIME.
//
// While a sentence is pending, the composition holds the converted sentence
// with the reading of the character being typed inserted at the cursor.
// Typing a code and pressing Space (or an end key) adds a character position
// with all of the code's candidates instead of opening the candidate window;
// the conversion then re-picks the candidates of the whole sentence.
//
//   Enter / Space (no reading)  commit the sentence
//   Esc (no reading)            discard the sentence
//   Backspace / Delete          remove the character before / under the cursor
//   Left / Right / Home / End   move the cursor between characters
//   Down                        choose the character under the cursor (pins it)
//   select keys / arrows in the candidate window pin the character
//
// Characters pinned by hand are learned on commit (_SentenceLearn): the code's
// preferred character and the pairs it forms with its neighbours, saved to
// %APPDATA%\DIME\SentenceUser.txt and used by later conversions.
//
// Anything that outputs text directly (punctuation, address keys, Shift+key
// English, unhandled keys) commits the sentence first.

#include "Private.h"
#include <Shlobj.h>
#include <Shlwapi.h>
#include "Globals.h"
#include "DIME.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "File.h"
#include "RustSessionHost.h"

namespace
{
	// %APPDATA%\DIME\<name> when present, otherwise <Program Files>\DIME\<name>.
	BOOL ResolveDataFile(_In_z_ LPCWSTR name, _Out_writes_(MAX_PATH) WCHAR* path)
	{
		WCHAR appData[MAX_PATH] = { 0 };
		WCHAR programFiles[MAX_PATH] = { 0 };

		if (Global::GetAppDataPath(appData, FALSE))
		{
			StringCchPrintf(path, MAX_PATH, L"%s\\DIME\\%s", appData, name);
			if (PathFileExists(path)) return TRUE;
		}
		if (GetEnvironmentVariable(L"ProgramW6432", programFiles, MAX_PATH) == 0)
			GetEnvironmentVariable(L"ProgramFiles", programFiles, MAX_PATH);
		StringCchPrintf(path, MAX_PATH, L"%s\\DIME\\%s", programFiles, name);
		return PathFileExists(path);
	}

	size_t LoadDataFile(_In_z_ LPCWSTR name, CSentenceLexicon* pLexicon, BOOL isFreqTable)
	{
		WCHAR path[MAX_PATH] = { 0 };
		if (!ResolveDataFile(name, path)) return 0;

		CFile file;
		if (!file.CreateFile(path, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ)) return 0;
		const WCHAR* pBuffer = file.GetReadBufferPointer();
		if (pBuffer == nullptr) return 0;
		size_t length = (size_t)(file.GetFileSize() / sizeof(WCHAR));
		return isFreqTable ? pLexicon->LoadFreqTable(pBuffer, length) : pLexicon->LoadCinValues(pBuffer, length);
	}
}

//+---------------------------------------------------------------------------
//
// _IsSentenceMode
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsSentencePending()
{
	return (_pRustSession && dime_session_sentence_pending(_pRustSession)) ? TRUE : FALSE;
}

BOOL CDIME::_IsSentenceMode()
{
	return CConfig::GetSentenceMode() && Global::imeMode == IME_MODE::IME_MODE_DAYI;
}

//+---------------------------------------------------------------------------
//
// _IsSentenceKeyEaten
//
// Called by _IsKeyEaten while a sentence is pending with no reading and no
// candidate window.  Returns TRUE (and the key state) for keys that act on
// the sentence; other keys take the normal path.
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _GetSentenceLexicon
//
// Loaded on first use: character frequencies from TCFreq.cin and the words of
// Array-Phrase.cin (the codes are ignored, only the words are used).
//----------------------------------------------------------------------------

CSentenceLexicon* CDIME::_GetSentenceLexicon()
{
	if (!_sentenceLexiconLoaded)
	{
		_sentenceLexiconLoaded = TRUE;
		if (CSentenceLexicon::IsSupported())
		{
			_pSentenceLexicon = new (std::nothrow) CSentenceLexicon();
			if (_pSentenceLexicon)
			{
				size_t freq = LoadDataFile(L"TCFreq.cin", _pSentenceLexicon, TRUE);
				size_t words = LoadDataFile(L"Array-Phrase.cin", _pSentenceLexicon, FALSE);
				debugPrint(L"CDIME::_GetSentenceLexicon() freq lines = %d, words = %d", (int)freq, (int)words);
			}

			_pSentenceUser = new (std::nothrow) CSentenceUserModel();
			_pSentencePending = new (std::nothrow) CSentenceUserModel();
			WCHAR path[MAX_PATH] = { 0 };
			if (_pSentenceUser && _SentenceUserFilePath(path))
				_pSentenceUser->LoadFile(path);
		}
	}
	return _pSentenceLexicon;
}

//+---------------------------------------------------------------------------
//
// _SentenceUserFilePath
//
// %APPDATA%\DIME\SentenceUser.txt: what sentence mode learned from the
// characters the user fixed by hand.  Delete the file to forget.
//----------------------------------------------------------------------------

BOOL CDIME::_SentenceUserFilePath(_Out_writes_(MAX_PATH) WCHAR* path)
{
	*path = L'\0';
	WCHAR appData[MAX_PATH] = { 0 };
	if (!Global::GetAppDataPath(appData, FALSE))
		return FALSE;
	return SUCCEEDED(StringCchPrintf(path, MAX_PATH, L"%s\\DIME\\SentenceUser.txt", appData));
}


//+---------------------------------------------------------------------------
//
// _SentenceLearn
//
// Called with the sentence about to be committed.  Characters the user fixed
// by hand are learned in memory right away and saved to the shared file: the
// file is re-read and only this process's new data added, so several
// processes do not overwrite each other.  Store apps cannot write %APPDATA%
// (read-only for AppContainer), so there the data stays in memory.
//----------------------------------------------------------------------------

void CDIME::_SentenceLearn()
{
	if (!_IsSentencePending() || _pSentenceUser == nullptr || _pSentencePending == nullptr)
		return;
	if (!dime_session_sentence_learn(_pRustSession, _pSentenceUser->Handle()))
		return;
	dime_session_sentence_learn(_pRustSession, _pSentencePending->Handle());

	if (_IsStoreAppMode() || isLowIntegrityProcess())
		return;

	WCHAR path[MAX_PATH] = { 0 };
	if (!_SentenceUserFilePath(path))
		return;

	CSentenceUserModel* pFresh = new (std::nothrow) CSentenceUserModel();
	if (pFresh == nullptr)
		return;
	pFresh->LoadFile(path);
	pFresh->Merge(*_pSentencePending);
	if (pFresh->SaveFile(path))
	{
		_pSentencePending->Clear();
		delete _pSentenceUser;
		_pSentenceUser = pFresh;  // now also includes what other processes learned
	}
	else
	{
		delete pFresh;  // keep the pending data for the next save
	}
}

std::wstring CDIME::_GetKeystrokeCode()
{
	std::wstring code;
	if (_pCompositionProcessorEngine)
	{
		DWORD_PTR len = _pCompositionProcessorEngine->GetVirtualKeyLength();
		for (DWORD_PTR i = 0; i < len; i++)
			code += _pCompositionProcessorEngine->GetVirtualKey(i);
	}
	return code;
}

//+---------------------------------------------------------------------------
//
// _SentenceWrite
//
// Replace the composition text and put the caret at caret.
//----------------------------------------------------------------------------

HRESULT CDIME::_SentenceWrite(TfEditCookie ec, _In_ ITfContext *pContext, const std::wstring& text, size_t caret, BOOL withAttributes)
{
	if (_pComposition == nullptr || pContext == nullptr) return S_FALSE;

	ITfRange* pRange = nullptr;
	HRESULT hr = _pComposition->GetRange(&pRange);
	if (FAILED(hr) || pRange == nullptr) return hr;

	_sentenceDisplay = text;
	hr = pRange->SetText(ec, 0, _sentenceDisplay.c_str(), (LONG)_sentenceDisplay.length());
	if (SUCCEEDED(hr))
	{
		lastReadingString.Set(_sentenceDisplay.c_str(), _sentenceDisplay.length());

		if (withAttributes)
		{
			_SetCompositionLanguage(ec, pContext);
			_SetCompositionDisplayAttributes(ec, pContext, _gaDisplayAttributeInput);
		}

		ITfRange* pSelection = nullptr;
		if (SUCCEEDED(pRange->Clone(&pSelection)) && pSelection)
		{
			LONG shifted = 0;
			pSelection->Collapse(ec, TF_ANCHOR_START);
			if (caret > 0) pSelection->ShiftEnd(ec, (LONG)caret, &shifted, nullptr);
			pSelection->Collapse(ec, TF_ANCHOR_END);

			TF_SELECTION sel;
			sel.range = pSelection;
			sel.style.ase = TF_AE_NONE;
			sel.style.fInterimChar = FALSE;
			pContext->SetSelection(ec, 1, &sel);
			pSelection->Release();
		}
	}
	pRange->Release();
	return hr;
}


//+---------------------------------------------------------------------------
//
// _SentenceAppend
//
// Add the character for the current reading at the cursor, re-convert and
// show the sentence.  The reading and any candidate window are cleared.
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _SentenceSelectCandidate
//
// A candidate was finalized from the candidate window (Space, Enter, select
// key, or typing on).  Either it fixes the character being edited (Down), or
// it adds the character for the current reading.  isExplicit pins it.
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _SentenceOpenCandidates
//
// Down: show the candidates of the character under the cursor.
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _SentenceCloseReading
//
// Drop the reading / candidate window but keep the sentence.
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _HandleSentenceCommit
//
// Write the sentence into the document and end the composition.
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _HandleSentenceBackspace
//
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _HandleSentenceArrowKey
//
//----------------------------------------------------------------------------


//+---------------------------------------------------------------------------
//
// _SentenceFinalizeInComposition
//
// The composition is being ended by someone else (focus change, mode
// switch, deactivation): leave the converted sentence in the document
// instead of the raw reading.
//----------------------------------------------------------------------------

void CDIME::_SentenceFinalizeInComposition(TfEditCookie ec)
{
	if (!_IsSentencePending() || _pComposition == nullptr)
		return;

	_SentenceLearn();

	ITfRange* pRange = nullptr;
	if (SUCCEEDED(_pComposition->GetRange(&pRange)) && pRange)
	{
		std::wstring text;
		if (DimeBuffer* pText = dime_session_sentence_text(_pRustSession))
		{
			size_t len = 0;
			const uint16_t* p = dime_buffer_data(pText, &len);
			if (p) text.assign((const WCHAR*)p, len);
			dime_buffer_free(pText);
		}
		pRange->SetText(ec, 0, text.c_str(), (LONG)text.length());
		pRange->Release();
	}
	_SentenceReset();
}

void CDIME::_SentenceReset()
{
	dime_session_sentence_reset(_pRustSession);
	_sentenceDisplay.clear();
	lastReadingString.Set(nullptr, 0);
}

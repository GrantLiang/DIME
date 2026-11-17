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
//#define DEBUG_PRINT
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "PhoneticComposition.h"

//+---------------------------------------------------------------------------
//
// AddVirtualKey
// Add virtual key code to Composition Processor Engine for used to parse keystroke data.
// param
//     [in] uCode - Specify virtual key code.
// returns
//     State of Text Processor Engine.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::AddVirtualKey(WCHAR wch)
{
	int beep = -1;
	BOOL ret = _AddVirtualKeyRust(wch, &beep);
	if (beep >= 0 && _pTextService)
		_pTextService->DoBeep((BEEP_TYPE)beep);
	return ret;
}

// AddVirtualKey via dime_add_key (rust/crates/dime-core/src/composition.rs).
// The buffer stays a C++ allocation, replaced on every change as before.
BOOL CCompositionProcessorEngine::_AddVirtualKeyRust(WCHAR wch, _Out_ int* pBeep)
{
	DimeAddKeyInput in = {};
	in.keys = (const uint16_t*)_keystrokeBuffer.Get();
	in.len = _keystrokeBuffer.GetLength();
	in.null = _keystrokeBuffer.Get() == nullptr ? 1 : 0;
	in.ime_mode = (uint32_t)Global::imeMode;
	in.array_scope = (uint32_t)CConfig::GetArrayScope();
	in.phonetic_eten = CConfig::getPhoneticKeyboardLayout() == PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT ? 1 : 0;
	in.max_codes = CConfig::GetMaxCodes();
	in.syllable = phoneticSyllable;

	DimeAddKeyResult r = {};
	dime_add_key(&in, wch, &r);
	*pBeep = r.beep;
	phoneticSyllable = r.syllable;

	if (r.action != 0)
	{
		DWORD_PTR oldLen = _keystrokeBuffer.GetLength();
		DWORD_PTR newLen = (r.action == 1) ? oldLen + 1 : r.replace_len;
		PWCHAR pwch = new (std::nothrow) WCHAR[(r.action == 1) ? newLen : 5];
		if (!pwch)
			return FALSE;
		if (r.action == 1)
		{
			if (oldLen)
				memcpy(pwch, _keystrokeBuffer.Get(), oldLen * sizeof(WCHAR));
			pwch[oldLen] = r.append;
		}
		else
		{
			memcpy(pwch, r.replace, newLen * sizeof(WCHAR));
			pwch[newLen] = L'\0';
		}
		if (_keystrokeBuffer.Get())
			delete[] _keystrokeBuffer.Get();
		_keystrokeBuffer.Set(pwch, newLen);
	}
	return r.accepted ? TRUE : FALSE;
}


//+---------------------------------------------------------------------------
//
// RemoveVirtualKey
// Remove stored virtual key code.
// param
//     [in] dwIndex   - Specified index.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::RemoveVirtualKey(DWORD_PTR dwIndex)
{
	_RemoveVirtualKeyRust(dwIndex);
}

void CCompositionProcessorEngine::_RemoveVirtualKeyRust(DWORD_PTR dwIndex)
{
	DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
	size_t newLen = dime_remove_key((uint16_t*)_keystrokeBuffer.Get(), srgKeystrokeBufLen, dwIndex);
	_keystrokeBuffer.Set(_keystrokeBuffer.Get(), newLen);
	if (srgKeystrokeBufLen == 1)
		_hasWildcardIncludedInKeystrokeBuffer = FALSE;
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
		phoneticSyllable = dime_phonetic_remove_last(phoneticSyllable);
}


//+---------------------------------------------------------------------------
//
// PurgeVirtualKey
// Purge stored virtual key code.
// param
//     none.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PurgeVirtualKey()
{
	//debugPrint(L"CCompositionProcessorEngine::PurgeVirtualKey()");
	if (_keystrokeBuffer.Get())
	{
		delete[] _keystrokeBuffer.Get();
		_keystrokeBuffer.Set(NULL, 0);

	}
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC) phoneticSyllable = 0;
	_hasWildcardIncludedInKeystrokeBuffer = FALSE;
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex)
{
	//debugPrint(L"CCompositionProcessorEngine::GetVirtualKey()");
	if (dwIndex < _keystrokeBuffer.GetLength())
	{
		return *(_keystrokeBuffer.Get() + dwIndex);
	}
	return 0;
}

// (Removed: ValidateCompositionKeyChar is implemented inline in the header to avoid
// linkage dependency for the settings UI project.)

BOOL CCompositionProcessorEngine::isEndComposingKey(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::isEndComposingKey()");
	if (_keystrokeBuffer.Get() == nullptr || _pEndkey == nullptr)// || Global::imeMode != IME_MODE::IME_MODE_PHONETIC)
		goto exit;
	UINT len = (UINT)_keystrokeBuffer.GetLength();
	if (len == 0)
		goto exit;
	
	for (UINT i = 0; i < wcslen(_pEndkey); i++)
	{
		if (*(_pEndkey+i) == wch)
			return TRUE;
	}
	//if (wch == '3' || wch == '4' || wch == '6' || wch == '7')
	//	return TRUE;
exit:
	//debugPrint(L"CCompositionProcessorEngine::isEndComposingKey() reutrn FALSE");
	return FALSE;

}

UINT CCompositionProcessorEngine::addPhoneticKey(WCHAR* pwch)
{
	//debugPrint(L"CCompositionProcessorEngine::addPhoneticKey()");
	WCHAR c = towupper(*pwch);
	if (c < 32 || c > MAX_RADICAL + 32) return phoneticSyllable;

	UINT s, m;
	if (CConfig::getPhoneticKeyboardLayout() == PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT)
		c = vpEtenKeyToStandardKeyTable[c - 32];

	s = vpStandardKeyTable[(c == 0) ? 0 : c - 32];


	if (s == vpAny)
	{
		if (phoneticSyllable == 0)
			phoneticSyllable |= vpAnyConsonant; //no syllable present, add wildcard as vpAnyConsonant.
		else if ((phoneticSyllable & vpConsonantMask) && !(phoneticSyllable & (vpMiddleVowelMask | vpVowelMask | vpToneMask)))
			phoneticSyllable |= vpAnyMiddleVowel; // consonant is present and no middle vowel present, add wildcard as vpAnyMiddleVowel
		else if (phoneticSyllable & (vpMiddleVowelMask) && !(phoneticSyllable & (vpVowelMask | vpToneMask)))
			phoneticSyllable |= vpAnyTone;
		else if (phoneticSyllable & (vpMiddleVowelMask | vpToneMask) && !(phoneticSyllable & vpVowelMask))
			phoneticSyllable |= vpAnyVowel;
		else if (phoneticSyllable & (vpMiddleVowelMask | vpVowelMask) && !(phoneticSyllable & vpToneMask))
			phoneticSyllable |= vpAnyTone;

		return phoneticSyllable;
	}


	if ((m = (s & vpToneMask)) != 0 && phoneticSyllable != 0) // do not add tones to empty syllable
		phoneticSyllable = (phoneticSyllable & (~vpToneMask)) | m;
	else
	{
		UINT oldSyllable = phoneticSyllable;
		if ((m = (s & vpVowelMask)) != 0)
			phoneticSyllable = (phoneticSyllable & (~vpVowelMask)) | m;
		else if ((m = (s & vpMiddleVowelMask)) != 0)
			phoneticSyllable = (phoneticSyllable & (~vpMiddleVowelMask)) | m;
		else if ((m = (s & vpConsonantMask)) != 0)
			phoneticSyllable = (phoneticSyllable & (~vpConsonantMask)) | m;

		if ((phoneticSyllable & vpToneMask) && oldSyllable != phoneticSyllable)
			phoneticSyllable = phoneticSyllable & (~vpToneMask);// clear tones if it's already present

	}

	return phoneticSyllable;

}
WCHAR CCompositionProcessorEngine::VPSymbolToStandardLayoutChar(UINT syllable)
{
	//debugPrint(L"CCompositionProcessorEngine::VPSymbolToStandardLayoutChar()");
	if (syllable == vpAnyConsonant || syllable == vpAnyMiddleVowel || syllable == vpAnyVowel || syllable == vpAnyTone)
		return '?';
	UINT oridinal = 0, ss;
	if ((ss = (syllable & vpConsonantMask)) != 0)
		oridinal = ss;
	else if ((ss = (syllable & vpMiddleVowelMask)) != 0)
		oridinal = (ss >> 5) + 21;
	else if ((ss = (syllable & vpVowelMask)) != 0)
		oridinal = (ss >> 8) + 24;
	else if ((ss = (syllable & vpToneMask)) != 0)
		oridinal = (ss >> 12) + 37;

	if (oridinal > 41) return 0;
	return vpStandardLayoutCode[oridinal];
}
UINT CCompositionProcessorEngine::removeLastPhoneticSymbol()
{
	//debugPrint(L"CCompositionProcessorEngine::removeLastPhoneticSymbol()");

	if (phoneticSyllable & vpToneMask)
		return (phoneticSyllable & ~vpToneMask);
	else if (phoneticSyllable & vpVowelMask)
		return (phoneticSyllable & ~vpVowelMask);
	else if (phoneticSyllable & vpMiddleVowelMask)
		return (phoneticSyllable & ~vpMiddleVowelMask);
	else
		return 0;
}
//+---------------------------------------------------------------------------
//
// IsEscapeInputLeading()
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsEscapeInputLeading()
{
	//debugPrint(L"CCompositionProcessorEngine::IsEscapeInputLeading()");
	if (_keystrokeBuffer.Get() == nullptr || _keystrokeBuffer.GetLength() ==0)
		return FALSE;
	WCHAR c = *_keystrokeBuffer.Get();
	if ((Global::imeMode == IME_MODE::IME_MODE_DAYI && c == L'=') ||
		(Global::imeMode == IME_MODE::IME_MODE_ARRAY && (
			(CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && towupper(c) == L'W') ||
			CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(c) == L'H' || c == L'8'))) ||
		(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && c == L'\\'))
		return TRUE;
	else
		return FALSE;
}
//+---------------------------------------------------------------------------
//
// IsEscapeInput
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsEscapeInput()
{
	return _IsEscapeInputRust();
}

BOOL CCompositionProcessorEngine::_IsEscapeInputRust()
{
	const WCHAR* p = _keystrokeBuffer.Get();
	return dime_is_escape_input((const uint16_t*)p, _keystrokeBuffer.GetLength(), p ? 1 : 0, p ? *p : 0,
		(uint32_t)Global::imeMode, (uint32_t)CConfig::GetArrayScope()) ? TRUE : FALSE;
}


//+---------------------------------------------------------------------------
//
// IsEscapeInputChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsEscapeInputChar(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::IsEscapeInputChar()");
	// Phonetic custom phrase mode: '\' as trigger, alphanumeric chars accepted as code
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC)
	{
		if (_keystrokeBuffer.GetLength() == 0 && wch == L'\\')
			return TRUE;
		// Allow all printable ASCII (including shifted punctuation) as part of phonetic escape composition,
		// but keep space excluded so space/enter still end/convert the escape input.
		if (_keystrokeBuffer.GetLength() >= 1 && _keystrokeBuffer.Get() && *_keystrokeBuffer.Get() == L'\\'
			&& IsDoubleSingleByte(wch) && wch != L' ')
			return TRUE;
	}
	if (_keystrokeBuffer.Get() == nullptr || _keystrokeBuffer.GetLength() > 2)  // > 2 to allow HG+digit (len==2)
		goto exit;
	debugPrint(L"CCompositionProcessorEngine::IsEscapeInputChar(), len = %d, c = %d", _keystrokeBuffer.GetLength(), *_keystrokeBuffer.Get());

	if (_keystrokeBuffer.GetLength() == 0)
	{
		if((Global::imeMode == IME_MODE::IME_MODE_DAYI && wch == L'=') ||
		   (Global::imeMode == IME_MODE::IME_MODE_ARRAY &&
				((CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(wch) == L'W')) ||
				 (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(wch) == L'H' || wch == L'8')))))
					return TRUE;
	}
	else if (_keystrokeBuffer.GetLength() == 1)
	{
		WCHAR c = *_keystrokeBuffer.Get();
		if (Global::imeMode == IME_MODE::IME_MODE_DAYI && towupper(c) == L'=')
		{
			return TRUE;
		}
		else if ( Global::imeMode == IME_MODE::IME_MODE_ARRAY)
		{
			if (CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 && (towupper(c) == L'W') &&
				(wch >= L'0' && wch <= L'9'))
				return TRUE;
			else if (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY40_BIG5
				&& (towupper(c) == L'H' || c == L'8') &&
				(wch == L'\'' || wch == L'[' || wch == L']' || wch == L'-' || wch == L'='))
				return TRUE;

		}
	}
	else if (_keystrokeBuffer.GetLength() == 2)
	{
		// Array non-BIG5: HG + digit → symbol input (hg0, hg1, ... hg9)
		WCHAR c1 = towupper(*_keystrokeBuffer.Get());
		WCHAR c2 = towupper(*(_keystrokeBuffer.Get() + 1));
		if (Global::imeMode == IME_MODE::IME_MODE_ARRAY &&
			CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5 &&
			c1 == L'H' && c2 == L'G' && wch >= L'0' && wch <= L'9')
			return TRUE;
	}
exit:
	debugPrint(L"CCompositionProcessorEngine::IsEscapeInputChar() return FALSE");
	return FALSE;
}

//+---------------------------------------------------------------------------
//
// IsDayiAddressChar
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsDayiAddressChar(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::IsDayiAddressChar()");
	if (Global::imeMode != IME_MODE::IME_MODE_DAYI)
		return FALSE;

	for (int i = 0; i < ARRAYSIZE(Global::dayiAddressCharTable); i++)
	{
		if (Global::dayiAddressCharTable[i]._Code == wch)
		{
			return TRUE;
		}
	}
	//debugPrint(L"CCompositionProcessorEngine::IsDayiAddressChar() return FALSE");
	return FALSE;
}


//+---------------------------------------------------------------------------
//
// IsArrayShortCode
//
//----------------------------------------------------------------------------
BOOL CCompositionProcessorEngine::IsArrayShortCode()
{
	//debugPrint(L"CCompositionProcessorEngine::IsArrayShortCode()");
	if (Global::imeMode == IME_MODE::IME_MODE_ARRAY && CConfig::GetArrayScope()!=ARRAY_SCOPE::ARRAY40_BIG5 &&
		_keystrokeBuffer.GetLength() < 3) 
		return TRUE;
	else
		return FALSE;
}


//+---------------------------------------------------------------------------
//
// IsDoubleSingleByte
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsDoubleSingleByte(WCHAR wch)
{
	//debugPrint(L"CCompositionProcessorEngine::IsDoubleSingleByte()");
	if (L' ' <= wch && wch <= L'~')
	{
		return TRUE;
	}
	return FALSE;
}


//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//	   [in] hasCandidateWithWildcard - Specified if candi with wildcard
//     [in] candiCount     - The number of candiddates in candi window
//     [in] candiSelection - The current selected index in candi window
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR* pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, UINT candiCount, INT candiSelection, _Inout_opt_ _KEYSTROKE_STATE* pKeyState)
{
	hasCandidateWithWildcard;
	return _IsVirtualKeyNeedRust(uCode, pwch, fComposing, candidateMode, candiCount, candiSelection, pKeyState);
}

//+---------------------------------------------------------------------------
//
// _IsVirtualKeyNeedRust
//
// The same decision made by dime_classify_key (rust/crates/dime-core/src/keyclass.rs)
// from a snapshot of the engine state, globals, config and the Win32 reads the
// C++ version makes. Parity: KeyClassifyParityTest.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::FillKeyInput(_Out_ CKeyInputSnapshot& snapshot, UINT uCode, WCHAR wch, BOOL fComposing, CANDIDATE_MODE candidateMode, UINT candiCount, INT candiSelection, BOOL hasKeyState)
{
	snapshot = {};
	UINT keystrokeCount = (UINT)min(_KeystrokeComposition.Count(), (size_t)ARRAYSIZE(snapshot.functions));
	for (UINT i = 0; i < keystrokeCount; i++)
		snapshot.functions[i] = (uint32_t)_KeystrokeComposition.GetAt(i)->Function;

	UINT selkeyCount = 0;
	if (_pActiveCandidateListIndexRange)
	{
		selkeyCount = (UINT)min(_pActiveCandidateListIndexRange->Count(), (int)ARRAYSIZE(snapshot.selkeys));
		for (UINT i = 0; i < selkeyCount; i++)
		{
			const _KEYSTROKE* pKS = _pActiveCandidateListIndexRange->GetAt(i);
			snapshot.selkeys[i].printable = pKS->Printable;
			snapshot.selkeys[i].index = pKS->Index;
			snapshot.selkeys[i].modifiers = pKS->Modifiers;
		}
	}

	DimeKeyInput& in = snapshot.in;
	in.code = uCode;
	in.wch = wch;
	in.base_char = (WCHAR)MapVirtualKey(uCode, MAPVK_VK_TO_CHAR);
	in.wch_printable = iswprint(wch) ? 1 : 0;
	in.composing = fComposing ? 1 : 0;
	in.candidate_mode = (uint32_t)candidateMode;
	in.candi_count = candiCount;
	in.candi_selection = candiSelection;
	in.has_key_state = hasKeyState ? 1 : 0;
	in.modifiers = Global::ModifiersValue;
	in.caps_on = (GetKeyState(VK_CAPITAL) & 0x0001) ? 1 : 0;
	in.ime_mode = (uint32_t)Global::imeMode;
	in.dictionary_available = IsDictionaryAvailable(_imeMode) ? 1 : 0;
	in.array_scope = (uint32_t)CConfig::GetArrayScope();
	in.numeric_pad = (uint32_t)CConfig::GetNumericPad();
	in.space_as_page_down = CConfig::GetSpaceAsPageDown() ? 1 : 0;
	in.shift_english_digit_mode = (uint32_t)CConfig::GetShiftEnglishDigitMode();
	in.phonetic_eten = CConfig::getPhoneticKeyboardLayout() == PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT ? 1 : 0;
	in.candidate_page_size = _candidatePageSize;
	in.has_wildcard_in_buffer = _hasWildcardIncludedInKeystrokeBuffer ? 1 : 0;
	in.buffer_null = _keystrokeBuffer.Get() == nullptr ? 1 : 0;
	in.buffer = (const uint16_t*)_keystrokeBuffer.Get();
	in.buffer_len = (uint32_t)_keystrokeBuffer.GetLength();
	in.keystroke_functions = snapshot.functions;
	in.keystroke_count = keystrokeCount;
	in.end_keys = (const uint16_t*)_pEndkey;
	in.end_keys_len = _pEndkey ? (uint32_t)wcslen(_pEndkey) : 0;
	in.selkeys = snapshot.selkeys;
	in.selkey_count = selkeyCount;
}

BOOL CCompositionProcessorEngine::_IsVirtualKeyNeedRust(UINT uCode, _In_reads_(1) WCHAR* pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, UINT candiCount, INT candiSelection, _Inout_opt_ _KEYSTROKE_STATE* pKeyState)
{
	CKeyInputSnapshot snapshot;
	FillKeyInput(snapshot, uCode, *pwch, fComposing, candidateMode, candiCount, candiSelection, pKeyState != nullptr);

	uint32_t category = 0, function = 0;
	BOOL eaten = dime_classify_key(&snapshot.in, &category, &function) ? TRUE : FALSE;
	if (pKeyState)
	{
		pKeyState->Category = (KEYSTROKE_CATEGORY)category;
		pKeyState->Function = (KEYSTROKE_FUNCTION)function;
	}
	return eaten;
}

//+---------------------------------------------------------------------------
//
// _IsVirtualKeyNeedCpp
//
//----------------------------------------------------------------------------

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE* pKeyState, KEYSTROKE_FUNCTION function)
{
	//debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition() pwch=%c, uCode=%d, function=%d", *pwch, uCode, function);

	
	if (!IsDictionaryAvailable(_imeMode) || pKeyState == nullptr || _KeystrokeComposition.Count() == 0)
	{
		return FALSE;
	}

	pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
	pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;


	_KEYSTROKE* pKeystroke = nullptr;


	WCHAR c = towupper(*pwch);
	if (c < 32 || c > 32 + MAX_RADICAL) return FALSE;

	if (CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_NUMERIC_COMPOSITION_ONLY &&
		!(uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
		return FALSE;

	//Convert ETEN keyboard to standard keyboard first if keyboard layout is ETEN
	if (Global::imeMode == IME_MODE::IME_MODE_PHONETIC && CConfig::getPhoneticKeyboardLayout() == PHONETIC_KEYBOARD_LAYOUT::PHONETIC_ETEN_KEYBOARD_LAYOUT)
	{
		c = vpEtenKeyToStandardKeyTable[c - 32];
		if (c == 0) c = 32;  // set c = 32 (0 after -32) when key is not mapped.
	}
	//Check if valid keystroke in keystroke table
	pKeystroke = _KeystrokeComposition.GetAt(c - 32);
	//debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition() mapped keystroke=%c", pKeystroke->Printable);

	if (c >= 'A' && c <= 'Z' && (Global::ModifiersValue & (TF_MOD_LSHIFT | TF_MOD_SHIFT)) != 0
		&& !(Global::imeMode == IME_MODE::IME_MODE_PHONETIC && IsEscapeInputLeading())) return FALSE; //  input English with shift-a~z 

	if (pKeystroke != nullptr && pKeystroke->Function != KEYSTROKE_FUNCTION::FUNCTION_NONE &&
		!(CConfig::GetNumericPad() == NUMERIC_PAD::NUMERIC_PAD_NUMERIC && uCode >= VK_NUMPAD0 && uCode <= VK_DIVIDE))
	{
		if (function == KEYSTROKE_FUNCTION::FUNCTION_NONE)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
			pKeyState->Function = pKeystroke->Function;
			//debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition() matched keystroke function=%d", pKeyState->Function);	
			return TRUE;
		}
		else if (function == pKeystroke->Function)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_COMPOSING;
			pKeyState->Function = pKeystroke->Function;
			//debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition() matched keystroke function=%d", pKeyState->Function);
			return TRUE;
		}
	}
	//debugPrint(L"CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition() no match");
	return FALSE;
}


//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, PWCH pwch, _Inout_opt_ _KEYSTROKE_STATE* pKeyState, CANDIDATE_MODE candidateMode)
{
	//debugPrint(L"IsKeystrokeRange() ucode=%d, pwch=%c, candidateMode=%d", uCode, *pwch, candidateMode);
	if (pKeyState == nullptr)
	{
		return FALSE;
	}

	pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_NONE;
	pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_NONE;

	if (_pActiveCandidateListIndexRange->IsRange(uCode, *pwch, Global::ModifiersValue, candidateMode))
	{

		if (candidateMode == CANDIDATE_MODE::CANDIDATE_PHRASE)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
		else if (candidateMode != CANDIDATE_MODE::CANDIDATE_NONE)
		{
			pKeyState->Category = KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE;
			pKeyState->Function = KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER;
			return TRUE;
		}
	}
	return FALSE;
}



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
#include <Shlobj.h>
#include <Shlwapi.h>
#include <sys/stat.h>
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "BaseStructure.h"
#include "Globals.h"
#include "KeyHandlerEditSession.h"


//+---------------------------------------------------------------------------
//
// SetupConfiguration
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration(IME_MODE imeMode)
{
	debugPrint(L"CCompositionProcessorEngine::SetupConfiguration() \n");
	_isKeystrokeSort = FALSE;
	_isWildCardWordFreqSort = TRUE;

	//Override options for specific IME.
	if (imeMode == IME_MODE::IME_MODE_DAYI)
	{
		CConfig::SetSpaceAsFirstCaniSelkey(TRUE);
	}
	else if (imeMode == IME_MODE::IME_MODE_ARRAY)
	{
		CConfig::SetMaxCodes(5);
		CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
		if (CConfig::GetArrayScope() != ARRAY_SCOPE::ARRAY40_BIG5)
			CConfig::SetAutoCompose(TRUE);
	}
	else if (imeMode == IME_MODE::IME_MODE_PHONETIC)
	{
		CConfig::SetSpaceAsFirstCaniSelkey(FALSE);
	}
	if (_pTableDictionaryEngine[(UINT)imeMode])
		_pEndkey = _pTableDictionaryEngine[(UINT)imeMode]->GetEndkey();

	return;
}


//+---------------------------------------------------------------------------
//
// SetupBig5Engine
//
// Creates (or recreates) a Big5-filtered CMemoryFile + CTableDictionaryEngine
// for the given imeMode slot, wrapping the already-loaded main dictionary
// file/engine for that slot.  Any previous Big5 engine/file is deleted first.
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupBig5Engine(IME_MODE imeMode, LCID locale)
{
	UINT slot = (UINT)imeMode;

	// Big5 engines are only needed for the currently active IME mode.
	// SetupDictionaryFile is also called for secondary modes by the reverse-conversion
	// provider; those loads must not trigger a Big5 engine build or debug dump.
	if (imeMode != Global::imeMode) return;

	if (_pBig5TableDictionaryEngine[slot])
	{
		delete _pBig5TableDictionaryEngine[slot];
		_pBig5TableDictionaryEngine[slot] = nullptr;
	}
	if (_pBig5TableDictionaryFile[slot])
	{
		delete _pBig5TableDictionaryFile[slot];
		_pBig5TableDictionaryFile[slot] = nullptr;
	}

	// Skip building when Big5 filtering is not active for this mode
	bool needsBig5 = (imeMode == IME_MODE::IME_MODE_ARRAY)
		? (CConfig::GetArrayScope() == ARRAY_SCOPE::ARRAY30_BIG5)
		: CConfig::GetBig5Filter();
	if (!needsBig5) return;

	if (!_pTableDictionaryEngine[slot] || !_pTableDictionaryFile[slot] ||
		!_pTableDictionaryFile[slot]->GetReadBufferPointer()) return;

	_pBig5TableDictionaryFile[slot] = new (std::nothrow) CMemoryFile(_pTableDictionaryFile[slot]);
	if (!_pBig5TableDictionaryFile[slot]) return;

	_pBig5TableDictionaryEngine[slot] = CTableDictionaryEngine::Create(
		locale, _pBig5TableDictionaryFile[slot], _pTableDictionaryEngine[slot]->GetDictionaryType());
	if (_pBig5TableDictionaryEngine[slot])
		_pBig5TableDictionaryEngine[slot]->ParseConfig(imeMode);
}

//+---------------------------------------------------------------------------
//
// SetupDictionaryFile
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupDictionaryFile(IME_MODE imeMode, LCID locale)
{
	return _SetupDictionaryFileRust(imeMode, locale);
}


void CCompositionProcessorEngine::ReleaseDictionaryFiles()  
{  
    for (UINT i = 0; i < IM_SLOTS; i++)  
    {  
        if (_pTableDictionaryFile[i])  
        {  
            delete _pTableDictionaryFile[i];  
            _pTableDictionaryFile[i] = nullptr;  
        }  
        if (_pCustomTableDictionaryFile[i])  
        {  
            delete _pCustomTableDictionaryFile[i];  
            _pCustomTableDictionaryFile[i] = nullptr;  
        }  
        _pTableDictionaryEngine[i] = nullptr;
        _pCustomTableDictionaryEngine[i] = nullptr;
        if (_pBig5TableDictionaryEngine[i])
        {
            delete _pBig5TableDictionaryEngine[i];
            _pBig5TableDictionaryEngine[i] = nullptr;
        }
        if (_pBig5TableDictionaryFile[i])
        {
            delete _pBig5TableDictionaryFile[i];
            _pBig5TableDictionaryFile[i] = nullptr;
        }
    }

    if (_pArrayShortCodeDictionaryFile)  
    {  
        delete _pArrayShortCodeDictionaryFile;  
        _pArrayShortCodeDictionaryFile = nullptr;  
    }  
    if (_pArrayShortCodeTableDictionaryEngine)
    {
        delete _pArrayShortCodeTableDictionaryEngine;
        _pArrayShortCodeTableDictionaryEngine = nullptr;
    }
    if (_pArraySpecialCodeDictionaryFile)  
    {  
        delete _pArraySpecialCodeDictionaryFile;  
        _pArraySpecialCodeDictionaryFile = nullptr;  
    }  
    if (_pArraySpecialCodeTableDictionaryEngine)  
    {  
        delete _pArraySpecialCodeTableDictionaryEngine;  
        _pArraySpecialCodeTableDictionaryEngine = nullptr;  
    }  
    if (_pArrayExtBDictionaryFile)  
    {  
        delete _pArrayExtBDictionaryFile;  
        _pArrayExtBDictionaryFile = nullptr;  
    }  
    if (_pArrayExtBTableDictionaryEngine)  
    {  
        delete _pArrayExtBTableDictionaryEngine;  
        _pArrayExtBTableDictionaryEngine = nullptr;  
    }  
    if (_pArrayExtCDDictionaryFile)  
    {  
        delete _pArrayExtCDDictionaryFile;  
        _pArrayExtCDDictionaryFile = nullptr;  
    }  
    if (_pArrayExtETableDictionaryEngine)  
    {  
        delete _pArrayExtETableDictionaryEngine;  
        _pArrayExtETableDictionaryEngine = nullptr;  
    }  
    if (_pTCSCTableDictionaryEngine)  
    {  
        delete _pTCSCTableDictionaryEngine;  
        _pTCSCTableDictionaryEngine = nullptr;  
    }  
    if (_pTCSCTableDictionaryFile)  
    {  
        delete _pTCSCTableDictionaryFile;  
        _pTCSCTableDictionaryFile = nullptr;  
    }  
    if (_pTCFreqTableDictionaryEngine)  
    {  
        delete _pTCFreqTableDictionaryEngine;  
        _pTCFreqTableDictionaryEngine = nullptr;  
    }  
    if (_pTCFreqTableDictionaryFile)  
    {  
        delete _pTCFreqTableDictionaryFile;  
        _pTCFreqTableDictionaryFile = nullptr;  
    }  
}

//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupKeystroke(IME_MODE imeMode)
{
	_SetupKeystrokeRust(imeMode);
}


void CCompositionProcessorEngine::GetVKeyFromPrintable(WCHAR printable, UINT* vKey, UINT* modifier)
{
	/*
	#define VK_OEM_1          0xBA   // ';:' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_COMMA      0xBC   // ',' any country
	#define VK_OEM_MINUS      0xBD   // '-' any country
	#define VK_OEM_PERIOD     0xBE   // '.' any country
	#define VK_OEM_2          0xBF   // '/?' for US
	#define VK_OEM_3          0xC0   // '`/~' for US
	#define VK_OEM_PLUS       0xBB   // '+' any country
	#define VK_OEM_4          0xDB  //  '[{' for US
	#define VK_OEM_5          0xDC  //  '\|' for US
	#define VK_OEM_6          0xDD  //  ']}' for US
	#define VK_OEM_7          0xDE  //  ''"' for US
	*/
	* modifier = 0;

	if ((printable >= '0' && printable <= '9') || (printable >= 'A' && printable <= 'Z'))
	{
		*vKey = printable;
	}
	else if (printable == '!')
	{
		*vKey = '1';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '@')
	{
		*vKey = '2';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '#')
	{
		*vKey = '3';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '$')
	{
		*vKey = '4';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '%')
	{
		*vKey = '5';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '^')
	{
		*vKey = '6';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '&')
	{
		*vKey = '7';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '*')
	{
		*vKey = '8';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '(')
	{
		*vKey = '9';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ')')
	{
		*vKey = '0';
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ',')
		*vKey = VK_OEM_COMMA;
	else if (printable == '<')
	{
		*vKey = VK_OEM_COMMA;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '.')
		*vKey = VK_OEM_PERIOD;
	else if (printable == '>')
	{
		*vKey = VK_OEM_PERIOD;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ';')
		*vKey = VK_OEM_1;
	else if (printable == ':')
	{
		*vKey = VK_OEM_1;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '/')
		*vKey = VK_OEM_2;
	else if (printable == '?')
	{
		*vKey = VK_OEM_2;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '`')
		*vKey = VK_OEM_3;
	else if (printable == '~')
	{
		*vKey = VK_OEM_3;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '[')
		*vKey = VK_OEM_4;
	else if (printable == '{')
	{
		*vKey = VK_OEM_4;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '\\')
		*vKey = VK_OEM_5;
	else if (printable == '|')
	{
		*vKey = VK_OEM_5;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == ']')
		*vKey = VK_OEM_6;
	else if (printable == '}')
	{
		*vKey = VK_OEM_6;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '\'')
		*vKey = VK_OEM_7;
	else if (printable == '"')
	{
		*vKey = VK_OEM_7;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '-')
		*vKey = VK_OEM_MINUS;
	else if (printable == '_')
	{
		*vKey = VK_OEM_MINUS;
		*modifier = TF_MOD_SHIFT;
	}
	else if (printable == '=')
		*vKey = VK_OEM_PLUS;
	else if (printable == '+')
	{
		*vKey = VK_OEM_PLUS;
		*modifier = TF_MOD_SHIFT;
	}
	else
	{
		*vKey = printable;
	}

}

//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	_SetupPreservedRust(pThreadMgr, tfClientId);
}


//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY& tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey* pXPreservedKey)
{
	if (pXPreservedKey == nullptr) return;
	pXPreservedKey->Guid = clsid;

	TF_PRESERVEDKEY* ptfPskey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
	if (!ptfPskey1)
	{
		return;
	}
	*ptfPskey1 = tfPreservedKey;

	size_t srgKeystrokeBufLen = 0;
	if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
	{
		return;
	}
	pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
	if (!pXPreservedKey->Description)
	{
		return;
	}

	StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen, pwszDescription);

	return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hot key.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey* pXPreservedKey, _In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	if (pThreadMgr == nullptr || pXPreservedKey == nullptr) return FALSE;
	ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

	if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
	{
		return FALSE;
	}

	if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr) != S_OK || pKeystrokeMgr == nullptr)
	{
		return FALSE;
	}

	for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
	{
		TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
		preservedKey.uModifiers &= 0xffff;

		size_t lenOfDesc = 0;
		if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
		{
			return FALSE;
		}
		pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey, pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc));
	}

	pKeystrokeMgr->Release();

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY>* pTSFPreservedKeyTable)
{
	if (pTSFPreservedKeyTable == nullptr) return FALSE;
	for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
	{
		TF_PRESERVEDKEY* ptfPskey = pTSFPreservedKeyTable->GetAt(i);

		if (((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
			((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
			((ptfPskey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly))
		{
			return FALSE;
		}
	}

	return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::OnPreservedKey(REFGUID rguid, _Out_ BOOL* pIsEaten, _In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	_OnPreservedKeyRust(rguid, pIsEaten, pThreadMgr, tfClientId);
}


BOOL CCompositionProcessorEngine::SetupHanCovertTable()
{
	return _SetupHanCovertTableRust();
}


BOOL CCompositionProcessorEngine::SetupTCFreqTable()
{
	return _SetupTCFreqTableRust();
}

BOOL CCompositionProcessorEngine::GetTCFromSC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	stringToConvert; convertedString;
	//not yet implemented
	return FALSE;
}
int CCompositionProcessorEngine::GetTCFreq(CStringRange* stringToFind)
{
	debugPrint(L"CCompositionProcessorEngine::GetTCFreq()");

	if (_pTCFreqTableDictionaryEngine == nullptr) SetupTCFreqTable();
	if (_pTCFreqTableDictionaryEngine == nullptr) return -1;

	if (stringToFind->GetLength() > 1) return (int)stringToFind->GetLength();

	CDIMEArray<CCandidateListItem> candidateList;
	_pTCFreqTableDictionaryEngine->CollectWord(stringToFind, &candidateList);
	if (candidateList.Count() == 1)
	{
		return _wtoi(candidateList.GetAt(0)->_ItemString.Get());
	}
	else
	{
		return 0;
	}


}

BOOL CCompositionProcessorEngine::GetSCFromTC(CStringRange* stringToConvert, CStringRange* convertedString)
{
	debugPrint(L"CCompositionProcessorEngine::GetSCFromTC()");
	if (!CConfig::GetDoHanConvert() || stringToConvert == nullptr || convertedString == nullptr) return FALSE;

	if (_pTCSCTableDictionaryEngine == nullptr) SetupHanCovertTable();
	if (_pTCSCTableDictionaryEngine == nullptr) return FALSE;

	UINT lenToConvert = (UINT)stringToConvert->GetLength();
	PWCHAR pwch = new (std::nothrow) WCHAR[static_cast<size_t>(lenToConvert) + 1];
	if (pwch == nullptr) return FALSE;
	*pwch = L'\0';
	CStringRange wcharToCover;

	for (UINT i = 0; i < lenToConvert; i++)
	{
		CDIMEArray<CCandidateListItem> candidateList;
		_pTCSCTableDictionaryEngine->CollectWord(&wcharToCover.Set(stringToConvert->Get() + i, 1), &candidateList);
		if (candidateList.Count() == 1)
			StringCchCatN(pwch, static_cast<size_t>(lenToConvert) + 1, candidateList.GetAt(0)->_ItemString.Get(), 1);
		else
			StringCchCatN(pwch, static_cast<size_t>(lenToConvert) + 1, wcharToCover.Get(), 1);
	}
	convertedString->Set(pwch, lenToConvert);
	return TRUE;
}

//+---------------------------------------------------------------------------
//
// GetDictionaryFile
//
//----------------------------------------------------------------------------

CFile* CCompositionProcessorEngine::GetDictionaryFile() const
{
	return _pTableDictionaryFile[(UINT)Global::imeMode];
}


//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr* pThreadMgr)
{
	ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

	if (IsEqualGUID(Guid, GUID_NULL))
	{
		return FALSE;
	}

	if (pThreadMgr && FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&pKeystrokeMgr)))
	{
		return FALSE;
	}

	if (pKeystrokeMgr == nullptr)
	{
		return FALSE;
	}

	for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
	{
		TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
		pPreservedKey.uModifiers &= 0xffff;

		pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
	}

	pKeystrokeMgr->Release();

	return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
	Guid = GUID_NULL;
	Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
	ITfThreadMgr* pThreadMgr = nullptr;

	HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
	if (SUCCEEDED(hr) && pThreadMgr)
	{
		UninitPreservedKey(pThreadMgr);
		pThreadMgr->Release();
		pThreadMgr = nullptr;
	}

	if (Description)
	{
		delete[] Description;
	}
}

void CCompositionProcessorEngine::SetupCandidateListRange(IME_MODE imeMode)
{
	_SetupCandidateListRangeRust(imeMode);
}


// Rust owns the load / keystroke / range / preserved-key decisions. This host
// applies them to the CFile and CTableDictionaryEngine members. Callback order
// matches DimeSetupHostVtbl in dime_ffi.h and rust/crates/dime-ffi/src/setup.rs.

static_assert(sizeof(DimeKeystrokeOut) == 20, "DimeKeystrokeOut abi");

static void FillKeystroke(_KEYSTROKE* dst, const DimeKeystrokeOut& src)
{
	if (!dst) return;
	dst->Index = src.index;
	dst->Printable = (WCHAR)src.printable;
	dst->CandIndex = (WCHAR)src.cand_index;
	dst->VirtualKey = src.virtual_key;
	dst->Modifiers = src.modifiers;
	dst->Function = (KEYSTROKE_FUNCTION)src.function;
}

static bool TermPath(const uint16_t* path, size_t len, WCHAR* buf, size_t cap)
{
	if (!buf || cap == 0) return false;
	if (!path) len = 0;
	if (len >= cap) len = cap - 1;
	if (len) CopyMemory(buf, path, len * sizeof(WCHAR));
	buf[len] = L'\0';
	return true;
}

static void ResolveSetupPaths(WCHAR* app, WCHAR* pf)
{
	app[0] = L'\0';
	pf[0] = L'\0';
	if (GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH) == 0)
		GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
	Global::GetAppDataPath(app, TRUE);
}

struct CEngineSetupHost
{
	CCompositionProcessorEngine* eng;

	static CEngineSetupHost* Self(DimeSetupHost* h)
	{
		return (h && h->ctx) ? static_cast<CEngineSetupHost*>(h->ctx) : nullptr;
	}

	CFile** FileSlot(int32_t id)
	{
		if (!eng || id < 0) return nullptr;
		int kind = id / 16;
		int slot = id % 16;
		switch (kind)
		{
		case 0: return (slot < IM_SLOTS) ? &eng->_pTableDictionaryFile[slot] : nullptr;
		case 1: return (slot < IM_SLOTS) ? &eng->_pCustomTableDictionaryFile[slot] : nullptr;
		case 2: return &eng->_pPhraseDictionaryFile;
		case 3: return &eng->_pArrayExtBDictionaryFile;
		case 4: return &eng->_pArrayExtCDDictionaryFile;
		case 5: return &eng->_pArrayExtEDictionaryFile;
		case 6: return &eng->_pArrayPhraseDictionaryFile;
		case 7: return &eng->_pArraySpecialCodeDictionaryFile;
		case 8: return &eng->_pArrayShortCodeDictionaryFile;
		case 9: return &eng->_pTCSCTableDictionaryFile;
		case 10: return &eng->_pTCFreqTableDictionaryFile;
		default: return nullptr;
		}
	}

	CTableDictionaryEngine** EngineSlot(int32_t id)
	{
		if (!eng || id < 0) return nullptr;
		int kind = id / 16;
		int slot = id % 16;
		switch (kind)
		{
		case 0: return (slot < IM_SLOTS) ? &eng->_pTableDictionaryEngine[slot] : nullptr;
		case 1: return (slot < IM_SLOTS) ? &eng->_pCustomTableDictionaryEngine[slot] : nullptr;
		case 2: return &eng->_pPhraseTableDictionaryEngine;
		case 3: return &eng->_pArrayExtBTableDictionaryEngine;
		case 4: return &eng->_pArrayExtCDTableDictionaryEngine;
		case 5: return &eng->_pArrayExtETableDictionaryEngine;
		case 6: return &eng->_pArrayPhraseTableDictionaryEngine;
		case 7: return &eng->_pArraySpecialCodeTableDictionaryEngine;
		case 8: return &eng->_pArrayShortCodeTableDictionaryEngine;
		case 9: return &eng->_pTCSCTableDictionaryEngine;
		case 10: return &eng->_pTCFreqTableDictionaryEngine;
		default: return nullptr;
		}
	}

	static int PathExists(DimeSetupHost* h, const uint16_t* path, size_t len)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return 0;
		return PathFileExistsW(buf) ? 1 : 0;
	}

	static int FileMtime(DimeSetupHost* h, const uint16_t* path, size_t len, int64_t* mtime)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return 0;
		struct _stat st;
		if (_wstat(buf, &st) != 0) return 0;
		if (mtime) *mtime = (int64_t)st.st_mtime;
		return 1;
	}

	static void CopyFileCb(DimeSetupHost* h, const uint16_t* src, size_t slen, const uint16_t* dst, size_t dlen, int failIfExists)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		WCHAR s[MAX_PATH], d[MAX_PATH];
		if (!TermPath(src, slen, s, MAX_PATH) || !TermPath(dst, dlen, d, MAX_PATH)) return;
		CopyFileW(s, d, failIfExists ? TRUE : FALSE);
	}

	static void CreateDirCb(DimeSetupHost* h, const uint16_t* path, size_t len)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return;
		CreateDirectoryW(buf, nullptr);
	}

	static int PathEqual(DimeSetupHost* h, uint32_t locale, const uint16_t* a, size_t alen, const uint16_t* b, size_t blen)
	{
		if (!Self(h)) return 0;
		if (alen == 0 && blen == 0) return 1;
		if (!a || !b) return 0;
		if (alen > 0x7fffffff) alen = 0x7fffffff;
		if (blen > 0x7fffffff) blen = 0x7fffffff;
		int r = CompareStringW((LCID)locale, NORM_IGNORECASE, (LPCWSTR)a, (int)alen, (LPCWSTR)b, (int)blen);
		return r == CSTR_EQUAL ? 1 : 0;
	}

	static int HasFile(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		return (s && *s) ? 1 : 0;
	}

	static int HasEngine(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CTableDictionaryEngine** s = self->EngineSlot(id);
		return (s && *s) ? 1 : 0;
	}

	static int DictType(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return -1;
		CTableDictionaryEngine** s = self->EngineSlot(id);
		return (s && *s) ? (int)(*s)->GetDictionaryType() : -1;
	}

	static int FileUpdated(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		return (s && *s && (*s)->IsFileUpdated()) ? 1 : 0;
	}

	static size_t FileName(DimeSetupHost* h, int32_t id, uint16_t* buf, size_t cap)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !buf || cap == 0) return 0;
		CFile** s = self->FileSlot(id);
		if (!s || !*s || !(*s)->GetFileName()) return 0;
		size_t n = wcslen((*s)->GetFileName());
		size_t w = n < cap ? n : cap;
		if (w) CopyMemory(buf, (*s)->GetFileName(), w * sizeof(uint16_t));
		return w;
	}

	static void DropFile(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		CFile** s = self->FileSlot(id);
		if (!s || !*s) return;
		delete *s;
		*s = nullptr;
	}

	static void DropEngine(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		CTableDictionaryEngine** s = self->EngineSlot(id);
		if (!s || !*s) return;
		delete *s;
		*s = nullptr;
	}

	static int NewFile(DimeSetupHost* h, int32_t id)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		if (!s) return 0;
		*s = new (std::nothrow) CFile();
		return *s ? 1 : 0;
	}

	static int CreateFileCb(DimeSetupHost* h, int32_t id, const uint16_t* path, size_t len, uint32_t share)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** s = self->FileSlot(id);
		if (!s || !*s) return 0;
		WCHAR buf[MAX_PATH];
		if (!TermPath(path, len, buf, MAX_PATH)) return 0;
		return (*s)->CreateFile(buf, GENERIC_READ, OPEN_EXISTING, share) ? 1 : 0;
	}

	static int CreateEngine(DimeSetupHost* h, int32_t id, uint32_t locale, int32_t dictType)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return 0;
		CFile** file = self->FileSlot(id);
		CTableDictionaryEngine** eng = self->EngineSlot(id);
		if (!file || !*file || !eng) return 0;
		*eng = CTableDictionaryEngine::Create((LCID)locale, *file, (DICTIONARY_TYPE)dictType);
		return *eng ? 1 : 0;
	}

	static void ParseConfigCb(DimeSetupHost* h, int32_t id, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self) return;
		CTableDictionaryEngine** eng = self->EngineSlot(id);
		if (!eng || !*eng) return;
		(*eng)->ParseConfig((IME_MODE)mode);
	}

	static void AliasPhrase(DimeSetupHost* h, int32_t slot)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng || slot < 0 || slot >= IM_SLOTS) return;
		self->eng->_pPhraseTableDictionaryEngine = self->eng->_pTableDictionaryEngine[slot];
	}

	static void SetupBig5Cb(DimeSetupHost* h, int32_t mode, uint32_t locale)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupBig5Engine((IME_MODE)mode, (LCID)locale);
	}

	static void SetupKeystrokeCb(DimeSetupHost* h, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupKeystroke((IME_MODE)mode);
	}

	static void LoadConfigCb(DimeSetupHost* h, int32_t mode)
	{
		if (!Self(h)) return;
		CConfig::LoadConfig((IME_MODE)mode);
	}

	static void SetupConfigurationCb(DimeSetupHost* h, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupConfiguration((IME_MODE)mode);
	}

	static void SetupCandidateRangeCb(DimeSetupHost* h, int32_t mode)
	{
		CEngineSetupHost* self = Self(h);
		if (!self || !self->eng) return;
		self->eng->SetupCandidateListRange((IME_MODE)mode);
	}

	static int HasTextService(DimeSetupHost* h)
	{
		CEngineSetupHost* self = Self(h);
		return (self && self->eng && self->eng->_pTextService) ? 1 : 0;
	}

	static int ArrayScopeCb(DimeSetupHost* h)
	{
		if (!Self(h)) return 0;
		return (int)CConfig::GetArrayScope();
	}

	static int ActiveModeCb(DimeSetupHost* h)
	{
		if (!Self(h)) return 0;
		return (int)Global::imeMode;
	}

	static int DoHanConvertCb(DimeSetupHost* h)
	{
		if (!Self(h)) return 0;
		return CConfig::GetDoHanConvert() ? 1 : 0;
	}

	static const DimeSetupHostVtbl Vtbl;
};

const DimeSetupHostVtbl CEngineSetupHost::Vtbl = {
	PathExists,
	FileMtime,
	CopyFileCb,
	CreateDirCb,
	PathEqual,
	HasFile,
	HasEngine,
	DictType,
	FileUpdated,
	FileName,
	DropFile,
	DropEngine,
	NewFile,
	CreateFileCb,
	CreateEngine,
	ParseConfigCb,
	AliasPhrase,
	SetupBig5Cb,
	SetupKeystrokeCb,
	LoadConfigCb,
	SetupConfigurationCb,
	SetupCandidateRangeCb,
	HasTextService,
	ArrayScopeCb,
	ActiveModeCb,
	DoHanConvertCb,
};

static void BindSetup(CCompositionProcessorEngine* eng, DimeSetupHost* ffi, CEngineSetupHost* host)
{
	host->eng = eng;
	ffi->vtbl = &CEngineSetupHost::Vtbl;
	ffi->ctx = host;
}

BOOL CCompositionProcessorEngine::_SetupDictionaryFileRust(IME_MODE imeMode, LCID locale)
{
	WCHAR app[MAX_PATH] = {};
	WCHAR pf[MAX_PATH] = {};
	ResolveSetupPaths(app, pf);
	CEngineSetupHost host;
	DimeSetupHost ffi;
	BindSetup(this, &ffi, &host);
	return dime_setup_dictionary(&ffi, (int32_t)imeMode, (uint32_t)locale, 0,
		(const uint16_t*)app, wcslen(app), (const uint16_t*)pf, wcslen(pf)) ? TRUE : FALSE;
}

void CCompositionProcessorEngine::_SetupKeystrokeRust(IME_MODE imeMode)
{
	if (!IsDictionaryAvailable(imeMode)) return;
	CTableDictionaryEngine* table = _pTableDictionaryEngine[(UINT)imeMode];
	_T_RadicalMap* map = (table) ? table->GetRadicalMap() : nullptr;
	if (!table || !map || map->empty() || map->size() > MAX_RADICAL) return;

	uint16_t radicals[MAX_RADICAL];
	size_t radicalCount = 0;
	for (_T_RadicalMap::iterator it = map->begin(); it != map->end() && radicalCount < MAX_RADICAL; ++it)
		radicals[radicalCount++] = (uint16_t)it->first;

	const uint16_t* end = _pEndkey ? (const uint16_t*)_pEndkey : nullptr;
	size_t endLen = _pEndkey ? wcslen(_pEndkey) : 0;
	DimeKeystrokeOut rows[MAX_RADICAL + 1];
	int32_t inserted = 0;
	size_t count = dime_build_keystrokes((int32_t)imeMode, radicals, radicalCount, end, endLen,
		rows, MAX_RADICAL + 1, &inserted);
	if (count > MAX_RADICAL + 1) count = MAX_RADICAL + 1;

	if (inserted && map->find(L'=') == map->end())
	{
		WCHAR* eq = new (std::nothrow) WCHAR[2];
		if (eq)
		{
			eq[0] = L'=';
			eq[1] = L'\0';
			(*map)[L'='] = eq;
		}
	}

	_KeystrokeComposition.Clear();
	for (size_t i = 0; i < count; i++)
		FillKeystroke(_KeystrokeComposition.Append(), rows[i]);
}

BOOL CCompositionProcessorEngine::_SetupHanCovertTableRust()
{
	WCHAR app[MAX_PATH] = {};
	WCHAR pf[MAX_PATH] = {};
	ResolveSetupPaths(app, pf);
	CEngineSetupHost host;
	DimeSetupHost ffi;
	BindSetup(this, &ffi, &host);
	LCID locale = _pTextService ? _pTextService->GetLocale() : 0;
	return dime_setup_han_convert(&ffi, (uint32_t)locale,
		(const uint16_t*)app, wcslen(app), (const uint16_t*)pf, wcslen(pf)) ? TRUE : FALSE;
}

BOOL CCompositionProcessorEngine::_SetupTCFreqTableRust()
{
	WCHAR app[MAX_PATH] = {};
	WCHAR pf[MAX_PATH] = {};
	ResolveSetupPaths(app, pf);
	CEngineSetupHost host;
	DimeSetupHost ffi;
	BindSetup(this, &ffi, &host);
	LCID locale = _pTextService ? _pTextService->GetLocale() : 0;
	return dime_setup_tc_freq(&ffi, (uint32_t)locale,
		(const uint16_t*)app, wcslen(app), (const uint16_t*)pf, wcslen(pf)) ? TRUE : FALSE;
}

void CCompositionProcessorEngine::_SetupCandidateListRangeRust(IME_MODE imeMode)
{
	const WCHAR* sel = nullptr;
	size_t selLen = 0;
	UINT slot = (UINT)imeMode;
	if (slot < IM_SLOTS && _pTableDictionaryEngine[slot] && _pTableDictionaryEngine[slot]->GetSelkey())
	{
		sel = _pTableDictionaryEngine[slot]->GetSelkey();
		selLen = wcslen(sel);
	}
	DimeKeystrokeOut cand[16];
	DimeKeystrokeOut phrase[16];
	uint32_t page = 0;
	int32_t dayiMode = (imeMode == IME_MODE::IME_MODE_DAYI) ? (int32_t)CConfig::GetDayiSelkeyMode() : 0;
	size_t n = dime_build_candidate_range((int32_t)imeMode, (int32_t)Global::imeMode, dayiMode,
		CConfig::GetSpaceAsFirstCaniSelkey() ? 1 : 0, (int32_t)CConfig::GetArrayScope(),
		(const uint16_t*)sel, selLen, cand, phrase, 16, &page);
	_candidateListIndexRange.Clear();
	_phraseCandidateListIndexRange.Clear();
	_candidatePageSize = page;
	if (n > 16) n = 16;
	if (n > page) n = page;
	for (size_t i = 0; i < n; i++)
	{
		FillKeystroke(_candidateListIndexRange.Append(), cand[i]);
		FillKeystroke(_phraseCandidateListIndexRange.Append(), phrase[i]);
	}
}

void CCompositionProcessorEngine::_SetupPreservedRust(_In_ ITfThreadMgr* pThreadMgr, TfClientId tfClientId)
{
	int n = dime_preserved_key_count();
	for (int i = 0; i < n; i++)
	{
		DimePreservedKey key = {};
		uint16_t desc[64];
		if (!dime_preserved_key_at(i, &key, desc, 64)) continue;
		TF_PRESERVEDKEY pk = {};
		pk.uVKey = key.vkey;
		pk.uModifiers = key.modifiers;
		const GUID* guid = nullptr;
		XPreservedKey* slot = nullptr;
		const WCHAR* text = nullptr;
		WCHAR local[64];
		local[0] = L'\0';
		switch (key.id)
		{
		case 0:
			guid = &Global::DIMEGuidImeModePreserveKey;
			slot = &_PreservedKey_IMEMode;
			text = Global::ImeModeDescription;
			break;
		case 1:
			guid = &Global::DIMEGuidDoubleSingleBytePreserveKey;
			slot = &_PreservedKey_DoubleSingleByte;
			text = Global::DoubleSingleByteDescription;
			break;
		case 2:
			guid = &Global::DIMEGuidConfigPreserveKey;
			slot = &_PreservedKey_Config;
			break;
		case 3:
			guid = &Global::DIMEGuidHanConvertPreserveKey;
			slot = &_PreservedKey_HanConvert;
			break;
		default:
			continue;
		}
		if (key.desc_len != 0)
		{
			size_t cch = key.desc_len;
			if (cch >= 64) cch = 63;
			CopyMemory(local, desc, cch * sizeof(WCHAR));
			local[cch] = L'\0';
			text = local;
		}
		if (guid && text)
			SetPreservedKey(*guid, pk, text, slot);
	}
	InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_Config, pThreadMgr, tfClientId);
	InitPreservedKey(&_PreservedKey_HanConvert, pThreadMgr, tfClientId);
}

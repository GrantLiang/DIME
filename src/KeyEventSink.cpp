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

#include "Private.h"
#include "Globals.h"
#include "DIME.h"
#include "UIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "KeyHandlerEditSession.h"
#include "Compartment.h"
#include "dime_ffi.h"

// 0xF003, 0xF004 are the keys that the touch keyboard sends for next/previous
#define THIRDPARTY_NEXTPAGE  static_cast<WORD>(0xF003)
#define THIRDPARTY_PREVPAGE  static_cast<WORD>(0xF004)

// Because the code mostly works with VKeys, here map a WCHAR back to a VKKey for certain
// vkeys that the IME handles specially
__inline UINT VKeyFromVKPacketAndWchar(UINT vk, WCHAR wch)
{
    UINT vkRet = vk;
	
	if (LOWORD(vk) == VK_PACKET)
    {
        if (wch == L' ')
        {
            vkRet = VK_SPACE;
        }
        else if ((wch >= L'0') && (wch <= L'9'))
        {
            vkRet = static_cast<UINT>(wch);
        }
        else if ((wch >= L'a') && (wch <= L'z'))
        {
            vkRet = (UINT)(L'A') + ((UINT)(L'z') - static_cast<UINT>(wch));
        }
        else if ((wch >= L'A') && (wch <= L'Z'))
        {
            vkRet = static_cast<UINT>(wch);
        }
		
        else if (wch == THIRDPARTY_NEXTPAGE)
        {
            vkRet = VK_NEXT;
        }
        else if (wch == THIRDPARTY_PREVPAGE)
        {
            vkRet = VK_PRIOR;
        }
    }
    return vkRet;
}

//+---------------------------------------------------------------------------
//
// _IsKeyEaten
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsKeyEaten(_In_ ITfContext *pContext, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState)
{
	debugPrint(L"CDIME::_IsKeyEaten(), codein = %d", codeIn);
    pContext;
    _KEY_FACTS facts = _GatherKeyFacts(codeIn);
    return _DecideKeyEaten(facts, codeIn, pCodeOut, pwch, pKeyState);
}

//+---------------------------------------------------------------------------
//
// _GatherKeyFacts
//
// Everything _DecideKeyEaten needs from TSF and Win32: compartments, keyboard
// state, the candidate window. Reading these has no side effects.
//
//----------------------------------------------------------------------------

CDIME::_KEY_FACTS CDIME::_GatherKeyFacts(UINT codeIn)
{
    _KEY_FACTS facts = {};

    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentIMEMode);
    CompartmentKeyboardOpen._GetCompartmentBOOL(facts.isOpen);

    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._GetCompartmentBOOL(facts.isDoubleSingleByte);

    facts.isKeyboardDisabled = _IsKeyboardDisabled();
    if (facts.isKeyboardDisabled)
        return facts;

    facts.wch = ConvertVKey(codeIn);
    facts.isComposing = _IsComposing();
    facts.candiSelection = -1;
    if (_pUIPresenter)
    {
        _pUIPresenter->GetCount(&facts.candiCount);
        facts.candiSelection = _pUIPresenter->_GetCandidateSelection();
    }
    return facts;
}

//+---------------------------------------------------------------------------
//
// _DecideKeyEaten
//
// Whether the IME wants the key and how it will handle it (pKeyState), from the
// gathered facts, the IME's own state and the composition engine. No TSF calls.
//
//----------------------------------------------------------------------------

BOOL CDIME::_DecideKeyEaten(const _KEY_FACTS& facts, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState)
{
    return _DecideKeyEatenRust(facts, codeIn, pCodeOut, pwch, pKeyState);
}

//+---------------------------------------------------------------------------
//
// _DecideKeyEatenRust
//
// dime_decide_key_eaten (rust/crates/dime-core/src/keyevent.rs) decides from
// the facts, the engine snapshot and the Rust session (sentence, Shift flags).
//----------------------------------------------------------------------------

BOOL CDIME::_DecideKeyEatenRust(const _KEY_FACTS& facts, UINT codeIn, _Out_ UINT *pCodeOut, _Out_writes_(1) WCHAR *pwch, _Inout_opt_ _KEYSTROKE_STATE *pKeyState)
{
    UINT codeOut = facts.isKeyboardDisabled ? codeIn : VKeyFromVKPacketAndWchar(codeIn, facts.wch);
    CCompositionProcessorEngine *pEngine = _pCompositionProcessorEngine;

    DimeDecideInput in = {};
    in.keyboard_disabled = facts.isKeyboardDisabled ? 1 : 0;
    in.is_open = facts.isOpen ? 1 : 0;
    in.is_double_single_byte = facts.isDoubleSingleByte ? 1 : 0;
    in.has_engine = pEngine ? 1 : 0;
    in.is_composing = facts.isComposing ? 1 : 0;
    in.candi_count = facts.candiCount;
    in.virtual_key_length = pEngine ? pEngine->GetVirtualKeyLength() : 0;

    CCompositionProcessorEngine::CKeyInputSnapshot snapshot;
    if (pEngine && !facts.isKeyboardDisabled)
    {
        pEngine->FillKeyInput(snapshot, codeOut, facts.wch, facts.isComposing, _candidateMode,
            facts.candiCount, facts.candiSelection, pKeyState != nullptr);
        in.key = snapshot.in;
    }
    else
    {
        in.key.code = codeOut;
        in.key.wch = facts.wch;
        in.key.has_key_state = pKeyState ? 1 : 0;
        in.key.candidate_mode = (uint32_t)_candidateMode;
    }

    DimeDecideOutput out = {};
    dime_decide_key_eaten(_pRustSession, &in, &out);

    if (out.reset_candidate_mode)
    {
        debugPrint(L"_IsKeyEaten: stale _candidateMode=%d with candiCount=0, resetting to NONE", (int)_candidateMode);
        _candidateMode = CANDIDATE_MODE::CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }
    *pCodeOut = codeOut;
    if (pwch)
        *pwch = out.wch;
    if (pKeyState)
    {
        pKeyState->Category = (KEYSTROKE_CATEGORY)out.category;
        pKeyState->Function = (KEYSTROKE_FUNCTION)out.function;
    }
    return out.eaten ? TRUE : FALSE;
}


//+---------------------------------------------------------------------------
//
// ConvertVKey
//
//----------------------------------------------------------------------------

WCHAR CDIME::ConvertVKey(UINT code)
{
    //
    // Map virtual key to scan code
    //
    UINT scanCode = 0;
    scanCode = MapVirtualKey(code, 0);

    //
    // Keyboard state
    //
    BYTE abKbdState[256] = {'\0'};
    if (!GetKeyboardState(abKbdState))
    {
        return 0;
    }

    //
    // Map virtual key to character code
    //
    WCHAR wch = '\0';
    if (ToUnicode(code, scanCode, abKbdState, &wch, 1, 0) == 1)
    {
        return wch;
    }

    return 0;
}

//+---------------------------------------------------------------------------
//
// _IsKeyboardDisabled
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsKeyboardDisabled()
{
    ITfDocumentMgr* pDocMgrFocus = nullptr;
    ITfContext* pContext = nullptr;
    BOOL isDisabled = FALSE;

    if ((_pThreadMgr && _pThreadMgr->GetFocus(&pDocMgrFocus) != S_OK) ||
        (pDocMgrFocus == nullptr))
    {
        // if there is no focus document manager object, the keyboard 
        // is disabled.
        isDisabled = TRUE;
    }
    else if ((pDocMgrFocus && pDocMgrFocus->GetTop(&pContext) != S_OK) ||
        (pContext == nullptr))
    {
        // if there is no context object, the keyboard is disabled.
        isDisabled = TRUE;
    }
    else
    {
        CCompartment CompartmentKeyboardDisabled(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_DISABLED);
        CompartmentKeyboardDisabled._GetCompartmentBOOL(isDisabled);

        CCompartment CompartmentEmptyContext(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_EMPTYCONTEXT);
        CompartmentEmptyContext._GetCompartmentBOOL(isDisabled);
    }

    if (pContext)
    {
        pContext->Release();
    }

    if (pDocMgrFocus)
    {
        pDocMgrFocus->Release();
    }

	debugPrint(L" CDIME::_IsKeyboardDisabled(), isDisabled = %d", isDisabled);

    return isDisabled;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnSetFocus
//
// Called by the system whenever this service gets the keystroke device focus.
//----------------------------------------------------------------------------

STDAPI CDIME::OnSetFocus(BOOL fForeground)
{
	fForeground;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnTestKeyDown
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnTestKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
 {
	debugPrint(L"OnTestKeyDown: vk=0x%02X _isShiftedEnglish=%d _pendingWildcard=%d", (UINT)wParam, _isShiftedEnglish, _pendingWildcardInput);
    Global::UpdateModifiers(wParam, lParam);

    // Windows 10: OnKeyDown and OnKeyUp are NEVER called for VK_SHIFT when the IME did not
    // eat the key — only OnTestKeyDown fires.  Detect a *fresh* Shift press by checking
    // lParam bit 30 (previous key-state: 0 = key was up, 1 = was already down / auto-repeat).
    // A fresh press means the user released and re-pressed Shift, ending the Shift gesture;
    // clear _isShiftedEnglish so that the subsequent Shift+wildcard enters composition.
    dime_session_on_test_key_down(_pRustSession, (uint32_t)wParam, (intptr_t)lParam);

	_KEYSTROKE_STATE KeystrokeState = { KEYSTROKE_CATEGORY::CATEGORY_NONE, KEYSTROKE_FUNCTION::FUNCTION_NONE };
    WCHAR wch = '\0';
    UINT code = 0;

	
	if (_pUIPresenter)
	{
		_pUIPresenter->ClearNotify();
		showChnEngNotify(_isChinese, 3000);
	}


    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, &KeystrokeState);

    uint32_t category = (uint32_t)KeystrokeState.Category, function = (uint32_t)KeystrokeState.Function;
    if (dime_test_key_down_invoke(*pIsEaten, &category, &function))
    {
        KeystrokeState.Category = (KEYSTROKE_CATEGORY)category;
        KeystrokeState.Function = (KEYSTROKE_FUNCTION)function;
        _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnKeyDown
//
// Called by the system to offer this service a keystroke.  If *pIsEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnKeyDown(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
	debugPrint(L"OnKeyDown: vk=0x%02X _isShiftedEnglish=%d _pendingWildcard=%d", (UINT)wParam, _isShiftedEnglish, _pendingWildcardInput);
    Global::UpdateModifiers(wParam, lParam);
   
	_KEYSTROKE_STATE KeystrokeState = { KEYSTROKE_CATEGORY::CATEGORY_NONE, KEYSTROKE_FUNCTION::FUNCTION_NONE };
    WCHAR wch = '\0';
    UINT code = 0;

	if (_pUIPresenter)
	{
		_pUIPresenter->ClearNotify();
		showChnEngNotify(_isChinese, 3000);
	}

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, &KeystrokeState);

    uint32_t category = (uint32_t)KeystrokeState.Category, function = (uint32_t)KeystrokeState.Function;
    if (dime_key_down_invoke(*pIsEaten, &category, &function, code, wch, (uint32_t)_candidateMode))
    {
        KeystrokeState.Category = (KEYSTROKE_CATEGORY)category;
        KeystrokeState.Function = (KEYSTROKE_FUNCTION)function;
        _InvokeKeyHandler(pContext, code, wch, (DWORD)lParam, KeystrokeState);
    }
    dime_session_after_key_down(_pRustSession, (uint32_t)KeystrokeState.Function, code, wch, Global::ModifiersValue);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnTestKeyUp
//
// Called by the system to query this service wants a potential keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnTestKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
	debugPrint(L" CDIME::OnTestKeyUp()");
    if (pIsEaten == nullptr)
    {
        return E_INVALIDARG;
    }

    Global::UpdateModifiers(wParam, lParam);

    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, NULL);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnKeyUp
//
// Called by the system to offer this service a keystroke.  If *pIsEaten == TRUE
// on exit, the application will not handle the keystroke.
//----------------------------------------------------------------------------

STDAPI CDIME::OnKeyUp(ITfContext *pContext, WPARAM wParam, LPARAM lParam, BOOL *pIsEaten)
{
	debugPrint(L"OnKeyUp: vk=0x%02X _isShiftedEnglish=%d _pendingWildcard=%d", (UINT)wParam, _isShiftedEnglish, _pendingWildcardInput);
    Global::UpdateModifiers(wParam, lParam);

    // Defense in depth: clear flags when Shift key-up is received.
    // NOTE: On Windows 10, OnKeyUp(VK_SHIFT) is also never called (TSF drops both
    // OnKeyDown and OnKeyUp for non-eaten keys).  The primary fix is in OnTestKeyDown
    // via the lParam bit-30 fresh-press detection.  This block handles any platform
    // where OnKeyUp(VK_SHIFT) does fire (e.g., future TSF changes or other edge cases).
    dime_session_on_key_up(_pRustSession, (uint32_t)wParam);

    WCHAR wch = '\0';
    UINT code = 0;

    *pIsEaten = _IsKeyEaten(pContext, (UINT)wParam, &code, &wch, NULL);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfKeyEventSink::OnPreservedKey
//
// Called when a hotkey (registered by us, or by the system) is typed.
//----------------------------------------------------------------------------

STDAPI CDIME::OnPreservedKey(ITfContext *pContext, REFGUID rguid, BOOL *pIsEaten)
{
	pContext;
	debugPrint(L" CDIME::OnPreservedKey()");
	
    CCompositionProcessorEngine *pCompositionProcessorEngine;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

	if(pCompositionProcessorEngine)
		pCompositionProcessorEngine->OnPreservedKey(rguid, pIsEaten, _GetThreadMgr(), _GetClientId());

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InitKeyEventSink
//
// Advise a keystroke sink.
//----------------------------------------------------------------------------

BOOL CDIME::_InitKeyEventSink()
{
	debugPrint(L"CDIME::_InitKeyEventSink()");
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;
    HRESULT hr = S_OK;

    if ( (_pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)) )|| pKeystrokeMgr==nullptr )
    {
		debugPrint(L"CDIME::_InitKeyEventSink() failed");
        return FALSE;
    }

    hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink *)this, TRUE);

    pKeystrokeMgr->Release();

    return (hr == S_OK);
}

//+---------------------------------------------------------------------------
//
// _UninitKeyEventSink
//
// Unadvise a keystroke sink.  Assumes we have advised one already.
//----------------------------------------------------------------------------

void CDIME::_UninitKeyEventSink()
{
	debugPrint(L"CDIME::_UninitKeyEventSink()");
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

    if ( ( _pThreadMgr && FAILED(_pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr))) || pKeystrokeMgr == nullptr)
    {
		debugPrint(L"CDIME::_UninitKeyEventSink() failed");
        return;
    }

    pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);

    pKeystrokeMgr->Release();
}

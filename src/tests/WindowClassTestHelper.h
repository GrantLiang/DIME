// WindowClassTestHelper.h - register DIME's window classes once per test process.
//
// Global::RegisterWindowClass() fails when a class is already registered and then
// leaves that class's atom at 0. Tests that need real windows call
// EnsureWindowClassesRegistered(), which recovers the atoms of classes that are
// already registered.
#pragma once

#include <Windows.h>
#include "../Globals.h"
#include "dime_ffi.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

// Tear a test window down as production does (UIPresenter: _Destroy, then delete), so no
// HWND, visible window or timer of it outlives the test (KeyFlow tests pump messages later).
template <class W>
inline void DestroyAndDelete(W*& w)
{
    if (w)
    {
        w->_Destroy();
        delete w;
        w = nullptr;
    }
}

inline void EnsureWindowClassesRegistered()
{
    if (!Global::dllInstanceHandle)
        Global::dllInstanceHandle = reinterpret_cast<HINSTANCE>(&__ImageBase);
    Global::RegisterWindowClass();

    struct ClassAtom { const wchar_t* name; ATOM* atom; };
    const ClassAtom classes[] = {
        { L"DIME.CandidateWindow", &Global::AtomCandidateWindow },
        { L"DIME.CandidateShadowWindow", &Global::AtomCandidateShadowWindow },
        { L"DIME.CandidateScrollBarWindow", &Global::AtomCandidateScrollBarWindow },
        { L"DIME.NotifyWindow", &Global::AtomNotifyWindow },
        { L"DIME.NotifyShadowWindow", &Global::AtomNotifyShadowWindow },
    };
    for (const ClassAtom& c : classes)
    {
        if (*c.atom != 0) continue;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        *c.atom = static_cast<ATOM>(GetClassInfoExW(Global::dllInstanceHandle, c.name, &wc));
    }
}

// A test window class with dime-ui's window procedure (popup: the candidate / notify
// styles; else the child / shadow class styles).
inline ATOM TestClassAtomKind(const wchar_t* name, bool popup)
{
    EnsureWindowClassesRegistered();
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    ATOM atom = (ATOM)GetClassInfoExW(Global::dllInstanceHandle, name, &wc);
    if (atom) return atom;
    return dime_ui_register_window_class(Global::dllInstanceHandle, (const uint16_t*)name, popup ? 1 : 0);
}
inline ATOM TestClassAtom(const wchar_t* name) { return TestClassAtomKind(name, false); }
inline ATOM RustScrollBarTestAtom() { return TestClassAtom(L"DIME.Test.RustScrollBarWindow"); }
// The classes of a test CRustNotifyWindow (_SetClassesForTest).
inline DimeNotifyClasses RustNotifyTestClasses()
{
    return { TestClassAtomKind(L"DIME.Test.RustNotifyWindow", true), TestClassAtom(L"DIME.Test.RustShadowWindow") };
}
// The classes of a test CRustCandidateWindow (_SetClassesForTest): test classes for the
// candidate window and its shadow / scroll bar, beside the ones registered for the real windows.
inline DimeCandClasses RustCandidateTestClasses()
{
    return { TestClassAtomKind(L"DIME.Test.RustCandidateWindow", true),
             TestClassAtom(L"DIME.Test.RustShadowWindow"),
             RustScrollBarTestAtom(),
             Global::AtomNotifyWindow, Global::AtomNotifyShadowWindow };
}

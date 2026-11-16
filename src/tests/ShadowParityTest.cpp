/********************************************************************************
* ShadowParityTest: the drop shadow of dime-ui::shadow against the golden files
* in src/tests/golden/Shadow, recorded while the C++ originals (ShadowCpp /
* CShadowWindow, removed in phase 6b-3) still matched them.
*  - Color: the shadow gray and peak alpha for light / dark themes and random
*    system window colors.
*  - Pixels: the premultiplied shadow image (rounded-rectangle SDF, double
*    precision) over window sizes, corner radii, grays and peak alphas.
*  - Behavior: a Rust shadow (dime_ui_shadow_create) around a Rust candidate
*    window, through random owner moves / resizes / show / hide, shadow show,
*    owner moved notifications and settings changes: the shadow's rectangle
*    relative to its owner, visibility, layered style, color, offset and
*    gradient flag, and the GDI calls (synthetic metrics, 96 dpi).
*  - ShadowMessagesAreNotDispatched: the shadow answers S_FALSE to every
*    message (as the C++ shadow did: it never set its UI window, so
*    CBaseWindow never dispatched to it).
*
* The random draws are the ones the recordings were made with: do not reorder
* them (DIME_GOLDEN_RECORD=1 re-records after a deliberate change).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "../BaseStructure.h"
#include "../Config.h"
#include "../Define.h"
#include "WindowClassTestHelper.h"
#include "RustCandidateWindow.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#ifdef DIME_RUST_AVAILABLE
#include "dime_ffi.h"
#include "Golden.h"

namespace
{
    HRESULT NoCallback(void*, enum CANDWND_ACTION) { return S_OK; }

    ATOM RustShadowAtom() { return TestClassAtom(L"DIME.Test.RustShadowWindow"); }
    const DWORD kShadowExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    const DWORD kShadowStyle = WS_DISABLED | WS_POPUP;
    // CBaseWindow's corner radius base of the candidate window (the owner).
    const int kOwnerCornerRadiusBase = 10;

    // The Rust shadow's environment: 96 dpi whatever the monitor.
    uint32_t GoldenDpi(void*) { return 96; }
    int32_t GoldenDark() { return CConfig::GetEffectiveDarkMode() ? 1 : 0; }
    const DimeShadowEnv kGoldenEnv = { GoldenDpi, GoldenDark };

    // Golden recordings: 96 dpi and a light system theme whatever the machine
    // (CConfig::GetDpiForHwnd / IsSystemDarkTheme).
    struct Dpi96
    {
        UINT saved = CConfig::testDpiOverride;
        int savedDark = CConfig::testSystemDarkOverride;
        Dpi96() { CConfig::testDpiOverride = 96; CConfig::testSystemDarkOverride = 0; }
        ~Dpi96() { CConfig::testDpiOverride = saved; CConfig::testSystemDarkOverride = savedDark; }
    };

    std::wstring GoldenState(DimeShadow* sh, HWND owner)
    {
        HWND h = (HWND)dime_ui_shadow_hwnd(sh);
        RECT rc = { 0, 0, 0, 0 }, orc = { 0, 0, 0, 0 };
        GetWindowRect(h, &rc);
        GetWindowRect(owner, &orc);
        uint32_t color = 0;
        DimePoint shift = { 0, 0 };
        int32_t gradient = 0;
        dime_ui_shadow_state(sh, &color, &shift, &gradient);
        return L"rect=" + std::to_wstring(rc.left - orc.left) + L"," + std::to_wstring(rc.top - orc.top) + L"," +
            std::to_wstring(rc.right - rc.left) + L"x" + std::to_wstring(rc.bottom - rc.top) +
            L" owner=" + std::to_wstring(orc.left) + L"," + std::to_wstring(orc.top) + L"," +
            std::to_wstring(orc.right - orc.left) + L"x" + std::to_wstring(orc.bottom - orc.top) +
            L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" layered=" + std::to_wstring((GetWindowLong(h, GWL_EXSTYLE) & WS_EX_LAYERED) != 0) +
            L" color=" + std::to_wstring(color) + L" shift=" + std::to_wstring(shift.x) + L"," + std::to_wstring(shift.y) +
            L" gradient=" + std::to_wstring(gradient);
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(ShadowParityTest)
    {
    public:
        TEST_METHOD(ColorMatchesCpp)
        {
            std::mt19937 rng(61);
            Golden::Log golden;
            Golden::Chunks ch(golden, L"colors", 2000);
            for (int i = 0; i < 20000; i++)
            {
                bool dark = rng() % 2;
                COLORREF sys = (i < 4) ? (i % 2 ? RGB(255, 255, 255) : RGB(0, 0, 0)) : (COLORREF)(rng() & 0xFFFFFF);
                BYTE gr = 0, ar = 0;
                dime_ui_shadow_color(dark ? 1 : 0, sys, &gr, &ar);
                ch.Add(std::to_wstring(dark) + L"," + std::to_wstring(sys) + L"=" + std::to_wstring(gr) + L"," + std::to_wstring(ar));
                ch.Next();
            }
            ch.Finish();
            Golden::Check(L"Shadow", L"Color", golden);
        }

        TEST_METHOD(PixelsMatchCpp)
        {
            std::mt19937 rng(62);
            const BYTE grays[] = { 0, 200 };
            const BYTE alphas[] = { 35, 45 };
            long long pixels = 0;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"images", 50);
            for (int i = 0; i < 600; i++)
            {
                // Owners at least 1 px: cx, cy >= 2 * SHADOW_SPREAD + 1.
                int cx = 2 * SHADOW_SPREAD + 1 + (int)(rng() % 420);
                int cy = 2 * SHADOW_SPREAD + 1 + (int)(rng() % 320);
                int radius = (int)(rng() % 41);
                BYTE gray = (i % 5 == 4) ? (BYTE)(rng() % 256) : grays[rng() % 2];
                BYTE maxAlpha = (i % 5 == 4) ? (BYTE)(rng() % 256) : alphas[rng() % 2];
                std::vector<DWORD> r((size_t)cx * cy, 0xDEADBEEF);
                dime_ui_shadow_pixels(cx, cy, SHADOW_SPREAD, radius, gray, maxAlpha, (uint32_t*)r.data(), r.size());
                ch.Add(std::to_wstring(cx) + L"x" + std::to_wstring(cy) + L" r=" + std::to_wstring(radius) + L" " +
                    std::to_wstring(gray) + L"," + std::to_wstring(maxAlpha));
                ch.Add(r.data(), r.size() * sizeof(DWORD));
                ch.Next();
                pixels += (long long)r.size();
            }
            ch.Finish();
            Golden::Check(L"Shadow", L"Pixels", golden);
            Logger::WriteMessage((std::to_wstring(pixels) + L" shadow pixels").c_str());
        }

        // The C++ shadow never set its UI window, so CBaseWindow::_WindowProc never
        // forwarded a message to it: after WM_CREATE every shadow message returned
        // S_FALSE without reaching the implementation (or DefWindowProc). The Rust
        // shadow keeps that (rather than "fixing" it).
        TEST_METHOD(ShadowMessagesAreNotDispatched)
        {
            EnsureWindowClassesRegistered();
            CCandidateRange range;
            range.Append()->CandIndex = L'1';
            CRustCandidateWindow w(NoCallback, nullptr, &range, FALSE);
            w._SetClassesForTest(RustCandidateTestClasses());
            Assert::IsTrue(w._Create(2, 12, nullptr) != FALSE);
            DimeShadow* sh = dime_ui_shadow_create(Global::dllInstanceHandle, RustShadowAtom(), kShadowExStyle, kShadowStyle,
                w._GetWnd(), kOwnerCornerRadiusBase, &kGoldenEnv);
            Assert::IsNotNull(sh);
            // A shadow of our own, and the candidate window's own shadow.
            for (HWND h : { (HWND)dime_ui_shadow_hwnd(sh), (HWND)dime_ui_candidate_child(w._GetRustWindow(), 1) })
            {
                Assert::IsNotNull(h);
                Assert::AreEqual((LRESULT)S_FALSE, SendMessage(h, WM_APP + 1, 0, 0), L"WM_APP+1 (dispatched: 0)");
                Assert::AreEqual((LRESULT)S_FALSE, SendMessage(h, WM_PAINT, 0, 0), L"WM_PAINT (dispatched: 0)");
                Assert::AreEqual((LRESULT)S_FALSE, SendMessage(h, WM_SETTINGCHANGE, 0, 0), L"WM_SETTINGCHANGE");
            }
            dime_ui_shadow_free(sh);
            w._Destroy();
        }

        TEST_METHOD(BehaviorMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            IME_COLOR_MODE savedMode = CConfig::GetColorMode();
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;

            std::mt19937 rng(63);
            int steps = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"steps", 30);
            for (int scenario = 0; scenario < 10; scenario++)
            {
                // The owner and its shadow exist only while the GDI calls are
                // recorded (synthetic text metrics size the owner).
                CCandidateRange grange;
                std::unique_ptr<CRustCandidateWindow> gw;
                DimeShadow* gsh = nullptr;
                {
                    Golden::GdiRecording rec;
                    for (int r = 0; r < 5; r++) grange.Append()->CandIndex = L'1' + (wchar_t)r;
                    gw = std::make_unique<CRustCandidateWindow>(NoCallback, nullptr, &grange, FALSE);
                    gw->_SetClassesForTest(RustCandidateTestClasses());
                    Assert::IsTrue(gw->_Create(2, 12, nullptr) != FALSE, L"the owner");
                    gw->_Move(300, 200);
                    gw->_ResizeWindow();
                    gsh = dime_ui_shadow_create(Global::dllInstanceHandle, RustShadowAtom(), kShadowExStyle, kShadowStyle,
                        gw->_GetWnd(), kOwnerCornerRadiusBase, &kGoldenEnv);
                    Assert::IsNotNull(gsh, L"the shadow");
                    std::vector<std::wstring> calls = rec.Take();
                    ch.Add(L"create " + GoldenState(gsh, gw->_GetWnd()));
                    for (const std::wstring& c : calls) ch.Add(c);
                    ch.Next();
                }
                for (int step = 0; step < 60; step++)
                {
                    int op = (int)(rng() % 9);
                    BOOL flag = rng() % 2;
                    int x = 100 + (int)(rng() % 600), y = 100 + (int)(rng() % 400);
                    int cx = 40 + (int)(rng() % 300), cy = 40 + (int)(rng() % 300);
                    bool dark = rng() % 2;
                    CConfig::SetColorMode(dark ? IME_COLOR_MODE::IME_COLOR_MODE_DARK : IME_COLOR_MODE::IME_COLOR_MODE_LIGHT);
                    HWND gh = gw->_GetWnd();
                    Golden::GdiRecording rec;
                    switch (op)
                    {
                    case 0: gw->_Move(x, y); break;
                    case 1: SetWindowPos(gh, nullptr, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE); break;
                    case 2: dime_ui_shadow_show(gsh, flag); break;
                    case 3: case 5: case 6: case 7: dime_ui_shadow_owner_moved(gsh, flag); break;
                    case 4: dime_ui_shadow_setting_change(gsh); break;
                    case 8: SetWindowPos(gh, nullptr, 0, 0, 0, 0,
                                SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | (flag ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)); break;
                    }
                    std::vector<std::wstring> calls = rec.Take();
                    ch.Add(L"op " + std::to_wstring(op) + L" " + GoldenState(gsh, gh));
                    for (const std::wstring& c : calls) ch.Add(c);
                    ch.Next();
                    steps++;
                }
                dime_ui_shadow_free(gsh);
                gw->_Destroy();
            }
            CConfig::SetColorMode(savedMode);
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            ch.Finish();
            Golden::Check(L"Shadow", L"Behavior", golden);
            Logger::WriteMessage((std::to_wstring(steps) + L" shadow steps").c_str());
        }
    };
}
#endif

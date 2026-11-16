/********************************************************************************
* SettingsFrameParityTest: WM_CREATE / WM_THEMECHANGED / WM_SETTINGCHANGE /
* WM_DPICHANGED of the settings window (dime-settings frame.rs through
* SettingsFrame::Rust). Every event kind over both dark flags, system
* themes, content present or not, several dpis and client sizes, plus random
* event sequences: the new dark flag and every effect (kind, argument,
* rectangle) must match the golden files (src/tests/golden/SettingsFrame),
* recorded when the C++ original (SettingsFrame::Cpp, removed in phase 6a-4)
* still matched them: every case, the initial sizes, and one hash per random
* sequence.
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <climits>
#include <random>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsFrame.h"
#include "Golden.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace SF = SettingsFrame;

namespace
{
    std::wstring D(const std::vector<SF::FrameEffect>& v, int dark)
    {
        std::wstring s = L"dark=" + std::to_wstring(dark);
        for (const auto& e : v)
            s += L" [" + std::to_wstring(e.kind) + L"," + std::to_wstring(e.arg) + L"," + std::to_wstring(e.rc.left) + L","
                + std::to_wstring(e.rc.top) + L"," + std::to_wstring(e.rc.right) + L"," + std::to_wstring(e.rc.bottom) + L"]";
        return s;
    }

    // Runs one event on `b` (a follows it); returns the number of effects.
    // `rust` gets the result for the golden file.
    size_t Check(SF::FrameState& a, SF::FrameState& b, const SF::FrameEvent& ev, const std::wstring&,
        std::wstring* rust = nullptr)
    {
        std::vector<SF::FrameEffect> eb;
        SF::Rust::Handle(b, ev, eb);
        a = b;
        if (rust) *rust = D(eb, b.isDark);
        return eb.size();
    }
}

namespace DIMETests
{
    TEST_CLASS(SettingsFrameParityTest)
    {
    public:
        TEST_METHOD(EveryEvent)
        {
            size_t effects = 0;
            int cases = 0;
            Golden::Log golden;
            for (int kind = 0; kind <= 5; kind++)
                for (int dark = 0; dark < 2; dark++)
                    for (int sys : { 0, 1, 2 })
                        for (int content = 0; content < 2; content++)
                            for (UINT dpi : { 96u, 120u, 144u, 192u, 0u })
                                for (int hasRect = 0; hasRect < 2; hasRect++) {
                                    SF::FrameEvent ev = {};
                                    ev.kind = kind;
                                    ev.systemDark = sys;
                                    ev.dpi = dpi;
                                    ev.hasContent = content;
                                    ev.clientW = 1100;
                                    ev.clientH = 780;
                                    ev.hasRect = hasRect;
                                    ev.rc = { 100, 50, 1300, 900 };
                                    SF::FrameState a = { dark }, b = { dark };
                                    std::wstring what = L"kind=" + std::to_wstring(kind) + L" dark=" + std::to_wstring(dark)
                                        + L" sys=" + std::to_wstring(sys) + L" content=" + std::to_wstring(content)
                                        + L" dpi=" + std::to_wstring(dpi) + L" rect=" + std::to_wstring(hasRect);
                                    std::wstring rust;
                                    effects += Check(a, b, ev, what, &rust);
                                    golden.Add(what + L": " + rust);
                                    cases++;
                                }
            Assert::IsTrue(effects > 1000, L"effects");
            for (UINT dpi : { 0u, 96u, 120u, 144u, 168u, 192u, 288u }) {
                SIZE b = SF::Rust::InitialSize(dpi);
                golden.Add(L"initial size dpi=" + std::to_wstring(dpi) + L": " + std::to_wstring(b.cx) + L"x"
                    + std::to_wstring(b.cy));
            }
            Golden::Check(L"SettingsFrame", L"EveryEvent", golden);
        }

        TEST_METHOD(RandomSequences)
        {
            std::mt19937 rng(5);
            size_t effects = 0;
            Golden::Log golden;
            for (int n = 0; n < 2000; n++) {
                SF::FrameState a = { (int)(rng() & 1) }, b = a;
                Golden::Hash hash;
                for (int step = 0; step < 12; step++) {
                    SF::FrameEvent ev = {};
                    ev.kind = 1 + (int)(rng() % 4);
                    ev.systemDark = (int)(rng() % 3);
                    ev.dpi = (UINT)(rng() % 400);
                    ev.hasContent = (int)(rng() & 1);
                    ev.clientW = (rng() % 50 == 0) ? INT_MIN : (int)(rng() % 3000) - 100;
                    ev.clientH = (int)(rng() % 2000);
                    ev.hasRect = (int)(rng() & 1);
                    ev.rc = { (int)(rng() % 4000) - 2000, (int)(rng() % 4000) - 2000, (int)(rng() % 4000), (int)(rng() % 4000) };
                    if (rng() % 50 == 0) ev.rc.right = INT_MIN;
                    std::wstring rust;
                    effects += Check(a, b, ev, L"sequence " + std::to_wstring(n) + L" step " + std::to_wstring(step), &rust);
                    hash.Add(rust);
                }
                golden.Add(L"sequence " + std::to_wstring(n) + L" " + hash.Hex());
            }
            Golden::Check(L"SettingsFrame", L"RandomSequences", golden);
            Assert::IsTrue(effects > 50000, L"effects");
        }
    };
}

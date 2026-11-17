/********************************************************************************
* NotifyParityTest: the notify window owned by Rust (dime-ui::notifywin /
* notify, through CRustNotifyWindow as UIPresenter holds it) against the golden
* files in src/tests/golden/Notify, recorded while the C++ CNotifyWindow
* (removed in phase 6b-3) still matched it side by side. The window exists only
* while its GDI calls are recorded with synthetic text metrics at 96 dpi.
*  - PaintMatchesCpp: the GDI calls, the new title size and the window size
*    (_ResizeWindow) over fonts, sizes, texts and colors.
*  - BehaviorMatchesCpp: a real notify window (WM_CREATE included) through
*    random _Show with delay-show / auto-hide, both timers, clicks inside /
*    outside, right clicks, mouse moves, WM_SETCURSOR, WM_MOUSEACTIVATE,
*    WM_POINTERACTIVATE, WM_WINDOWPOSCHANGING / CHANGED, WM_SHOWWINDOW,
*    WM_PAINT, real WM_TIMER messages and moves, for the 中/英 (clickable) and
*    reverse-lookup types: the state, visibility, position and size, shadow
*    visibility, callbacks, cursor, timers started / stopped and the results.
*
* The random draws are the ones the recordings were made with: do not reorder
* them (DIME_GOLDEN_RECORD=1 re-records after a deliberate change).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "../Globals.h"
#include "RustNotifyWindow.h"
#include "../BaseStructure.h"
#include "../UIConstants.h"
#include "WindowClassTestHelper.h"
#include "Golden.h"
#include "../Config.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    struct Calls
    {
        std::vector<std::wstring> log;
    };

    HRESULT RecordNotify(void* pv, enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam)
    {
        static_cast<Calls*>(pv)->log.push_back(std::to_wstring((int)action) + L"," + std::to_wstring(wParam) + L"," + std::to_wstring(lParam));
        return S_OK;
    }

    // Timers started / stopped (the window's hook; ctx: the list to append to).
    std::wstring TimerText(BOOL start, UINT elapse, UINT_PTR id)
    {
        return (start ? L"+" : L"-") + std::to_wstring(id) + (start ? L"@" + std::to_wstring(elapse) : L"");
    }
    void TimerHook(void* ctx, int32_t start, uint32_t elapse, uintptr_t id)
    {
        static_cast<std::vector<std::wstring>*>(ctx)->push_back(TimerText(start, elapse, id));
    }

    // Runs `paint` on a scratch bitmap of h's client size (the calls are what is recorded).
    void PaintInto(HWND h, const std::function<void(HDC, PAINTSTRUCT*)>& paint, bool withPs)
    {
        RECT rc = { 0, 0, 0, 0 };
        GetClientRect(h, &rc);
        int cx = max(1, (int)rc.right), cy = max(1, (int)rc.bottom);
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, cx, cy);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        PAINTSTRUCT ps = {};
        ps.hdc = mem;
        ps.rcPaint = rc;
        paint(mem, withPs ? &ps : nullptr);
        GdiFlush();
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }

    // Golden recordings: 96 dpi and a light system theme whatever the machine
    // (CConfig::GetDpiForHwnd / IsSystemDarkTheme).
    struct Dpi96
    {
        UINT saved = CConfig::testDpiOverride;
        int savedDark = CConfig::testSystemDarkOverride;
        Dpi96() { CConfig::testDpiOverride = 96; CConfig::testSystemDarkOverride = 0; }
        ~Dpi96() { CConfig::testDpiOverride = saved; CConfig::testSystemDarkOverride = savedDark; }
    };

    struct GoldenSide
    {
        Calls calls;
        std::vector<std::wstring> timers;
        CRustNotifyWindow* w = nullptr;
        ~GoldenSide()
        {
            if (w) { dime_ui_notifywin_set_timer_hook(w->_GetRustWindow(), nullptr, nullptr); w->_Destroy(); delete w; }
        }
        HWND Wnd() const { return w->_GetWnd(); }
    };

    std::wstring CursorName(HCURSOR c)
    {
        if (!c) return L"none";
        static const LPCWSTR ids[] = { IDC_ARROW, IDC_HAND, IDC_IBEAM };
        static const wchar_t* names[] = { L"arrow", L"hand", L"ibeam" };
        for (int i = 0; i < ARRAYSIZE(ids); i++)
            if (c == LoadCursor(nullptr, ids[i])) return names[i];
        return L"other";
    }

    std::wstring ObserveGolden(GoldenSide& g)
    {
        int st[6] = {};
        dime_ui_notifywin_state(g.w->_GetRustWindow(), (int32_t*)st);
        HWND h = g.Wnd(), shadow = (HWND)dime_ui_notifywin_shadow_hwnd(g.w->_GetRustWindow());
        RECT rc = { 0, 0, 0, 0 };
        GetWindowRect(h, &rc);
        std::wstring s = L"title=" + std::to_wstring(st[0]) + L"x" + std::to_wstring(st[1]) +
            L" pos=" + std::to_wstring(st[2]) + L"," + std::to_wstring(st[3]) +
            L" delay=" + std::to_wstring(st[4]) + L" hide=" + std::to_wstring(st[5]) +
            L" visible=" + std::to_wstring(IsWindowVisible(h)) +
            L" rect=" + std::to_wstring(rc.left) + L"," + std::to_wstring(rc.top) + L"," + std::to_wstring(rc.right - rc.left) + L"x" + std::to_wstring(rc.bottom - rc.top) +
            L" shadow=" + std::to_wstring(shadow ? IsWindowVisible(shadow) : -1) +
            L" width=" + std::to_wstring(g.w->_GetWidth()) + L" height=" + std::to_wstring(g.w->_GetHeight()) + L" calls=";
        for (auto& c : g.calls.log) s += c + L";";
        s += L" timers=";
        for (auto& t : g.timers) s += t + L" ";
        return s;
    }

    // The window's mouse (dime_ui_set_cursor_pos_for_test): at `pt` in h's client area,
    // whatever the machine's mouse does.
    void SetGoldenMouse(HWND h, int x, int y)
    {
        POINT p = { x, y };
        ClientToScreen(h, &p);
        DimePoint dp = { p.x, p.y };
        dime_ui_set_cursor_pos_for_test(&dp);
    }

    void AddStep(Golden::Chunks& ch, const std::wstring& head, const std::vector<std::wstring>& calls)
    {
        ch.Add(head);
        for (const std::wstring& c : calls) ch.Add(c);
        ch.Next();
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(NotifyParityTest)
    {
    public:
        TEST_METHOD(PaintMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            const wchar_t* faces[] = { L"Microsoft JhengHei", L"PMingLiU", L"Segoe UI" };
            const int heights[] = { -16, -24, -37 };
            const std::wstring pool[] = { L"中", L"英", L"全形", L"半形", L"ㄅㄆㄇ", L"a-b-c", L"反查：一二三四", L"特別碼 ABCD" };
            std::mt19937 rng(71);
            int painted = 0, sized = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"paints", 8);
            for (const wchar_t* face : faces)
            for (int height : heights)
            {
                HFONT font = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, face);
                Global::defaultlFontHandle = font;
                for (int s = 0; s < 8; s++)
                {
                    std::wstring text = pool[rng() % ARRAYSIZE(pool)];
                    COLORREF fg = rng() & 0xFFFFFF, bg = rng() & 0xFFFFFF, fill = rng() & 0xFFFFFF;
                    bool fillBrush = rng() % 4 != 0;
                    bool withPs = rng() % 6 != 0;
                    CStringRange sr;
                    sr.Set(text.c_str(), text.size());
                    std::wstring where = std::wstring(face) + L" " + std::to_wstring(height) + L" \"" + text + L"\"";

                    GoldenSide g;
                    Golden::GdiRecording rec;
                    g.w = new CRustNotifyWindow(RecordNotify, &g.calls, NOTIFY_TYPE::NOTIFY_CHN_ENG);
                    g.w->_SetClassesForTest(RustNotifyTestClasses());
                    Assert::IsTrue(g.w->_Create(12, nullptr, &sr) != FALSE);
                    Assert::IsNotNull(g.Wnd(), L"a real window");
                    g.w->_SetTextColor(fg, bg);
                    if (fillBrush) g.w->_SetFillColor(fill);
                    PaintInto(g.Wnd(), [&](HDC dc, PAINTSTRUCT* ps) {
                        DimeRect pr = {};
                        if (ps) pr = { ps->rcPaint.left, ps->rcPaint.top, ps->rcPaint.right, ps->rcPaint.bottom };
                        dime_ui_notifywin_paint(g.w->_GetRustWindow(), dc, ps ? &pr : nullptr);
                    }, withPs);
                    std::vector<std::wstring> calls = rec.Take();
                    AddStep(ch, where + L" " + ObserveGolden(g), calls);

                    if (g.w->_GetWidth() > 0) sized++;
                    painted++;
                }
                Global::defaultlFontHandle = savedFont;
                DeleteObject(font);
            }
            ch.Finish();
            Golden::Check(L"Notify", L"Paint", golden);
            Logger::WriteMessage((std::to_wstring(painted) + L" notify paints, " + std::to_wstring(sized) + L" with a non-empty window").c_str());
        }

        TEST_METHOD(BehaviorMatchesCpp)
        {
            EnsureWindowClassesRegistered();
            HFONT savedFont = Global::defaultlFontHandle;
            HFONT font = CreateFont(-24, 0, 0, 0, FW_NORMAL, FALSE, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft JhengHei");
            Global::defaultlFontHandle = font;
            std::mt19937 rng(72);
            int steps = 0, callbacks = 0;
            Dpi96 dpi96;
            Golden::Log golden;
            Golden::Chunks ch(golden, L"steps", 20);
            const NOTIFY_TYPE types[] = { NOTIFY_TYPE::NOTIFY_CHN_ENG, NOTIFY_TYPE::NOTIFY_REVERSE_LOOKUP };
            for (int scenario = 0; scenario < 12; scenario++)
            {
                std::wstring text = (scenario % 3 == 0) ? L"英" : L"反查：中文字";
                CStringRange sr;
                sr.Set(text.c_str(), text.size());
                GoldenSide g;
                {
                    Golden::GdiRecording rec;
                    g.w = new CRustNotifyWindow(RecordNotify, &g.calls, types[scenario % 2]);
                    g.w->_SetClassesForTest(RustNotifyTestClasses());
                    Assert::IsTrue(g.w->_Create(12, nullptr, (scenario % 4 == 3) ? nullptr : &sr) != FALSE);
                    if (scenario % 4 == 3) g.w->_SetString(&sr);
                    g.w->_SetTextColor(RGB(0, 0, 0), RGB(255, 255, 255));
                    g.w->_SetFillColor(RGB(255, 255, 255));
                    dime_ui_notifywin_set_timer_hook(g.w->_GetRustWindow(), TimerHook, &g.timers);
                    std::vector<std::wstring> calls = rec.Take();
                    AddStep(ch, L"create " + ObserveGolden(g), calls);
                }
                for (int step = 0; step < 80; step++)
                {
                    int op = (int)(rng() % 15);
                    UINT times[] = { 0, 0, 100, 500 };
                    UINT delay = times[rng() % 4], tth = times[rng() % 4];
                    BOOL flag = rng() % 2;
                    int x = (int)(rng() % 200) - 50, y = (int)(rng() % 80) - 20;
                    UINT_PTR timerIds[] = { UI::DELAY_SHOW_TIMER_ID, UI::TIME_TO_HIDE_TIMER_ID, 4242 };
                    UINT_PTR timer = timerIds[rng() % 3];
                    int mx = 200 + (int)(rng() % 400), my = 200 + (int)(rng() % 300);
                    static const WORD ev[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_MOUSEMOVE };
                    WORD mev = ev[rng() % 4];
                    UINT wpFlags = 0;
                    static const UINT fl[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_HIDEWINDOW, SWP_SHOWWINDOW, SWP_NOREDRAW };
                    for (UINT b : fl) if (rng() % 3 == 0) wpFlags |= b;

                    HWND h = g.Wnd();
                    HWND gs = (HWND)dime_ui_notifywin_shadow_hwnd(g.w->_GetRustWindow());
                    SetCursor(nullptr);
                    WINDOWPOS pos = {};
                    pos.hwnd = h;
                    pos.flags = wpFlags;
                    if (flag && gs) pos.hwndInsertAfter = gs;
                    LRESULT gr = 0;
                    SetGoldenMouse(h, x, y);
                    Golden::GdiRecording rec;
                    switch (op)
                    {
                    case 0: case 1: g.w->_Show(flag || op == 0, delay, tth); break;
                    case 2: dime_ui_notifywin_op(g.w->_GetRustWindow(), 4, (int32_t)timer, 0, 0); break;
                    case 3: gr = SendMessage(h, WM_LBUTTONDOWN, 0, MAKELPARAM(x, y)); break;
                    case 4: gr = SendMessage(h, WM_RBUTTONDOWN, 0, MAKELPARAM(x, y)); break;
                    case 5: gr = SendMessage(h, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)); break;
                    case 6: gr = SendMessage(h, WM_SETCURSOR, (WPARAM)h, MAKELPARAM(HTCLIENT, flag ? WM_MOUSEMOVE : WM_LBUTTONUP)); break;
                    case 7: gr = SendMessage(h, WM_MOUSEACTIVATE, 0, MAKELPARAM(HTCLIENT, mev)); break;
                    case 8: gr = SendMessage(h, 0x024B /* WM_POINTERACTIVATE */, 0, 0); break;
                    case 9: gr = SendMessage(h, flag ? WM_WINDOWPOSCHANGING : WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos); break;
                    case 10: gr = SendMessage(h, WM_SHOWWINDOW, flag, 0); break;
                    case 11: gr = SendMessage(h, WM_PAINT, 0, 0); break;
                    case 12: g.w->_Move(mx, my); break;
                    case 13: gr = SendMessage(h, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); break;
                    case 14: gr = SendMessage(h, WM_TIMER, timer, 0); break;   // routed to the timer object
                    }
                    std::vector<std::wstring> calls = rec.Take();
                    dime_ui_set_cursor_pos_for_test(nullptr);
                    AddStep(ch, L"op " + std::to_wstring(op) + L" " + ObserveGolden(g) + L" res=" + std::to_wstring(gr) +
                        L" cursor=" + CursorName(GetCursor()) + L" wp=" + std::to_wstring(pos.flags), calls);
                    steps++;
                }
                callbacks += (int)g.calls.log.size();
            }
            Global::defaultlFontHandle = savedFont;
            DeleteObject(font);
            ch.Finish();
            Golden::Check(L"Notify", L"Behavior", golden);
            Logger::WriteMessage((std::to_wstring(steps) + L" notify steps, " + std::to_wstring(callbacks) + L" callbacks").c_str());
            Assert::IsTrue(callbacks > 0, L"coverage");
        }
    };
}

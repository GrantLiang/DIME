/********************************************************************************
* SettingsScreenshotTest: screenshots of every settings page, C++
* (SettingsWindow) vs Rust (dime-settings window.rs), both over the recording
* SettingsTestApp::FakeApp (phase 5b-13, docs/RUST_MIGRATION_PLAN.md).
*
* Each test method is one theme x dpi. Both windows are shown fully
* transparent (layered, alpha 0, click-through, not activated). The picture
* is composed without DWM (PrintWindow(PW_RENDERFULLCONTENT) returns stale
* frames whenever DWM is not composing, e.g. with the display off): the main
* window and the content area through their WM_PAINT bodies, and the child
* controls (combo boxes, edit boxes, owner-drawn buttons, colour swatches,
* the RichEdit) and the scroll bar through WM_PRINT, in z-order, clipped to
* their parent. SettingsWindowParityTest compares only the WM_PAINT part. Pages: the
* first level of each IME, every card of each IME, and the load-table page, at a
* wide size and at a size below the sidebar collapse break. The two pictures
* must be identical pixel for pixel.
*
* On a mismatch the C++, Rust and diff pictures are saved as PNG under
* %TEMP%\DIMESettingsScreenshots. Set DIME_SETTINGS_SCREENSHOT_DIR to save
* every picture (for the manual review in docs/SETTINGS_MANUAL_CHECKLIST.md).
*
* Source is ASCII only (the project compiles with code page 950).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsWindow.h"
#include "../DIMESettings/SettingsController.h"
#include "../DIMESettings/SettingsPageLayout.h"
#include "dime_ffi.h"
#include "SettingsFakeApp.h"

#pragma comment(lib, "gdiplus.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace SettingsTestApp;

namespace
{
    const wchar_t kTestKey[] = L"SOFTWARE\\DIME\\SettingsScreenshotTest";
    const wchar_t kCaption[] = L"DIME \u8A2D\u5B9A v1.3.0";
    ULONG_PTR g_gdiplusToken = 0;

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 32-bit top-down picture.
    struct Shot {
        int w = 0, h = 0;
        std::vector<DWORD> px;
        DWORD At(int x, int y) const { return px[(size_t)y * w + x] & 0xFFFFFF; }
    };

    void WriteUtf16(const std::wstring& path, const std::wstring& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Assert::IsTrue(h != INVALID_HANDLE_VALUE, path.c_str());
        DWORD written = 0;
        WCHAR bom = 0xFEFF;
        WriteFile(h, &bom, sizeof(bom), &written, nullptr);
        WriteFile(h, text.data(), (DWORD)(text.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(h);
    }

    // A 32-bit top-down DIB selected into a memory DC.
    struct Dib {
        HDC dc = nullptr;
        HBITMAP bmp = nullptr;
        HGDIOBJ old = nullptr;
        void* bits = nullptr;
        int w = 0, h = 0;
        Dib(int width, int height) : w(width), h(height)
        {
            HDC screen = GetDC(nullptr);
            dc = CreateCompatibleDC(screen);
            ReleaseDC(nullptr, screen);
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth = w > 0 ? w : 1;
            bi.bmiHeader.biHeight = -(h > 0 ? h : 1);
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            old = SelectObject(dc, bmp);
        }
        ~Dib()
        {
            SelectObject(dc, old);
            DeleteObject(bmp);
            DeleteDC(dc);
        }
        Dib(const Dib&) = delete;
        Dib& operator=(const Dib&) = delete;
    };

    // Window rectangle of h relative to the window rectangle of `of`.
    RECT RectIn(HWND h, HWND of)
    {
        RECT r, o;
        GetWindowRect(h, &r);
        GetWindowRect(of, &o);
        OffsetRect(&r, -o.left, -o.top);
        return r;
    }

    // Client area of h relative to its window rectangle.
    RECT ClientIn(HWND h)
    {
        RECT c; GetClientRect(h, &c);
        POINT p = { 0, 0 };
        ClientToScreen(h, &p);
        RECT w; GetWindowRect(h, &w);
        OffsetRect(&c, p.x - w.left, p.y - w.top);
        return c;
    }

    void BlitClipped(HDC dst, const RECT& at, HDC src, const RECT& clip)
    {
        RECT r;
        if (!IntersectRect(&r, &at, &clip)) return;
        BitBlt(dst, r.left, r.top, r.right - r.left, r.bottom - r.top, src, r.left - at.left, r.top - at.top, SRCCOPY);
    }

    bool IsOurs(HWND h)
    {
        std::wstring c = ClassOf(h);
        return c == L"DIMEShotMain" || c == L"DIMEShotContent";
    }

    // Renders h (its whole window rectangle) without going through DWM:
    // the settings window's own two windows through their WM_PAINT bodies
    // (SettingsWindow::PaintForTest / dime_settings_window_paint), the
    // non-client area (the content area's scroll bar) and every standard
    // control through WM_PRINT. Visible children are drawn on top, bottom of
    // the z-order first, clipped to the parent's client area.
    void Render(HWND h, bool rust, Dib& out)
    {
        RECT client = ClientIn(h);
        if (!IsOurs(h)) {
            SendMessageW(h, WM_PRINT, (WPARAM)out.dc, PRF_NONCLIENT | PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
            GdiFlush();
            return;
        }
        SendMessageW(h, WM_PRINT, (WPARAM)out.dc, PRF_NONCLIENT);
        {
            int cw = client.right - client.left, ch = client.bottom - client.top;
            Dib body(cw, ch);
            bool content = ClassOf(h) == L"DIMEShotContent";
            if (cw > 0 && ch > 0) {
                if (rust) dime_settings_window_paint(h, body.dc, cw, ch, content ? 1 : 0);
                else SettingsWindow::PaintForTest(h, body.dc, cw, ch, content);
                GdiFlush();
                BitBlt(out.dc, client.left, client.top, cw, ch, body.dc, 0, 0, SRCCOPY);
            }
        }
        HWND first = GetWindow(h, GW_CHILD);
        for (HWND c = first ? GetWindow(first, GW_HWNDLAST) : nullptr; c; c = GetWindow(c, GW_HWNDPREV)) {
            if (!IsWindowVisible(c)) continue;
            RECT at = RectIn(c, h);
            Dib child(at.right - at.left, at.bottom - at.top);
            Render(c, rust, child);
            BlitClipped(out.dc, at, child.dc, client);
        }
        GdiFlush();
    }

    // The main window's client area, as the user sees it.
    Shot Capture(HWND main, bool rust)
    {
        Shot s;
        RECT client = ClientIn(main);
        s.w = client.right - client.left;
        s.h = client.bottom - client.top;
        if (s.w <= 0 || s.h <= 0) return s;
        RECT wr = RectIn(main, main);
        Dib whole(wr.right - wr.left, wr.bottom - wr.top);
        Render(main, rust, whole);
        Dib cut(s.w, s.h);
        BitBlt(cut.dc, 0, 0, s.w, s.h, whole.dc, client.left, client.top, SRCCOPY);
        GdiFlush();
        s.px.assign((const DWORD*)cut.bits, (const DWORD*)cut.bits + (size_t)s.w * s.h);
        return s;
    }

    bool PngEncoder(CLSID* out)
    {
        UINT n = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&n, &size);
        if (!size) return false;
        std::vector<BYTE> buf(size);
        Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
        Gdiplus::GetImageEncoders(n, size, info);
        for (UINT i = 0; i < n; i++)
            if (wcscmp(info[i].MimeType, L"image/png") == 0) { *out = info[i].Clsid; return true; }
        return false;
    }

    void SavePng(const Shot& s, const std::wstring& path)
    {
        if (s.px.empty()) return;
        CLSID png;
        if (!PngEncoder(&png)) return;
        std::vector<DWORD> opaque(s.px.size());
        for (size_t i = 0; i < s.px.size(); i++) opaque[i] = s.px[i] | 0xFF000000;
        Gdiplus::Bitmap bmp(s.w, s.h, s.w * 4, PixelFormat32bppARGB, (BYTE*)opaque.data());
        bmp.Save(path.c_str(), &png, nullptr);
    }

    std::wstring EnvDir()
    {
        wchar_t buf[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"DIME_SETTINGS_SCREENSHOT_DIR", buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return L"";
        CreateDirectoryW(buf, nullptr);
        return buf;
    }

    std::wstring FailDir()
    {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring d = std::wstring(tmp) + L"DIMESettingsScreenshots";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }

    // Number of differing pixels and their bounding box; diff is the C++
    // picture dimmed, with the differing pixels in magenta.
    int Compare(const Shot& a, const Shot& b, RECT& box, Shot& diff)
    {
        box = { INT_MAX, INT_MAX, -1, -1 };
        if (a.w != b.w || a.h != b.h) return -1;
        diff = a;
        int n = 0;
        for (int y = 0; y < a.h; y++)
            for (int x = 0; x < a.w; x++) {
                size_t i = (size_t)y * a.w + x;
                if (a.At(x, y) == b.At(x, y)) {
                    DWORD c = a.px[i];
                    diff.px[i] = ((c >> 2) & 0x3F3F3F) + 0x404040;
                    continue;
                }
                diff.px[i] = 0xFF00FF;
                n++;
                if (x < box.left) box.left = x;
                if (y < box.top) box.top = y;
                if (x + 1 > box.right) box.right = x + 1;
                if (y + 1 > box.bottom) box.bottom = y + 1;
            }
        return n;
    }

    // Average luminance of a block (0..255).
    int Luma(const Shot& s, int x0, int y0, int w, int h)
    {
        long long sum = 0, n = 0;
        for (int y = y0; y < y0 + h && y < s.h; y++)
            for (int x = x0; x < x0 + w && x < s.w; x++) {
                DWORD c = s.At(x, y);
                sum += (299 * ((c >> 16) & 0xFF) + 587 * ((c >> 8) & 0xFF) + 114 * (c & 0xFF)) / 1000;
                n++;
            }
        return n ? (int)(sum / n) : -1;
    }

    int Colors(const Shot& s)
    {
        std::vector<DWORD> seen;
        for (size_t i = 0; i < s.px.size() && seen.size() < 64; i += 7) {
            DWORD c = s.px[i] & 0xFFFFFF;
            bool found = false;
            for (DWORD x : seen) if (x == c) { found = true; break; }
            if (!found) seen.push_back(c);
        }
        return (int)seen.size();
    }

    struct Side {
        bool rust;
        FakeApp app;
        HWND main = nullptr;

        void Create(const std::wstring& dir, bool dark, UINT dpi, const SettingsSnapshot& snap)
        {
            app.dir = dir;
            app.dark = dark;
            app.dpi = dpi;
            app.snap = snap;
            std::wstring sfx = rust ? L"_R" : L"_C";
            std::wstring mainCls = L"DIMEShotMain" + sfx, contentCls = L"DIMEShotContent" + sfx;
            if (rust) {
                main = (HWND)dime_settings_window_create(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
                    static_cast<SettingsApp::Host*>(&app), GetModuleHandleW(nullptr), 0, (const uint16_t*)kCaption,
                    (const uint16_t*)mainCls.c_str(), (const uint16_t*)contentCls.c_str(), (const uint16_t*)kTestKey,
                    (const uint16_t*)L"R", 10);
            } else {
                main = SettingsWindow::CreateForTest(&app, GetModuleHandleW(nullptr), IME_MODE::IME_MODE_DAYI, kCaption,
                    mainCls.c_str(), contentCls.c_str(), kTestKey, L"C");
            }
            Assert::IsNotNull(main);
            // Fully transparent, click-through and not activated: nothing on
            // screen, no hover from the real mouse, no focus or caret.
            SetWindowLongW(main, GWL_EXSTYLE,
                GetWindowLongW(main, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
            SetLayeredWindowAttributes(main, 0, 0, LWA_ALPHA);
            SetWindowPos(main, nullptr, 40, 40, 1000, 760, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            Pump();
        }

        void Resize(int w, int h)
        {
            SetWindowPos(main, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
            Pump();
        }

        void Navigate(int mode, int sel, int level, int card)
        {
            if (rust) dime_settings_window_navigate(main, mode, sel, level, card);
            else SettingsWindow::NavigateForTest(main, (IME_MODE)mode, sel, level, card);
            Pump();
        }

        Shot Take()
        {
            Pump();
            return Capture(main, rust);
        }

        ~Side()
        {
            if (main) DestroyWindow(main);
            Pump();
        }
    };

    struct Page {
        int mode, sel, level, card;
        std::wstring Name() const
        {
            if (sel == 5) return L"loadtable";
            return L"ime" + std::to_wstring(sel) + (level ? L"_card" + std::to_wstring(card) : std::wstring(L"_home"));
        }
    };

    std::vector<Page> Pages()
    {
        int count = 0;
        const LayoutCard* cards = GetIMEPageLayout(&count);
        std::vector<Page> out;
        for (int i = 0; i < 4; i++) {
            int mode = (int)SettingsModel::IndexToMode(i);
            out.push_back({ mode, i, 0, 0 });
            for (int c = 0; c < count; c++)
                if (cards[c].action == RowAction::NavigateToCard) out.push_back({ mode, i, 1, c });
        }
        out.push_back({ (int)SettingsModel::IndexToMode(0), 5, 0, 0 });
        return out;
    }

    void Run(bool dark, UINT dpi)
    {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring dir = std::wstring(tmp) + L"DIMEShot" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);
        // A saved custom table with valid and invalid lines, so the editor
        // page shows coloured text.
        const std::wstring zhong(1, (wchar_t)0x4E2D), wen(1, (wchar_t)0x6587);
        const std::wstring table = L"a " + zhong + L"\r\nab " + zhong + wen + L"\r\nnospace\r\nabcdefghijk " + wen
            + L"\r\nx " + zhong;
        for (int m = 0; m < 16; m++) WriteUtf16(dir + L"\\custom" + std::to_wstring(m) + L".txt", table);
        std::wstring keep = EnvDir();
        std::wstring tag = std::wstring(dark ? L"dark" : L"light") + L"_" + std::to_wstring(dpi);

        SettingsSnapshot snap = SettingsModel::LoadFromConfig();
        Side c{ false }, r{ true };
        c.Create(dir, dark, dpi, snap);
        r.Create(dir, dark, dpi, snap);

        struct Size { const wchar_t* name; int w, h; };
        // Wide, and narrow (below sidebarCollapseBreak 800 x dpi / 96, above
        // the 520 x dpi / 96 minimum).
        const Size sizes[] = {
            { L"wide", MulDiv(1000, dpi, 96), MulDiv(720, dpi, 96) },
            { L"narrow", MulDiv(640, dpi, 96), MulDiv(720, dpi, 96) },
        };

        std::wstring failures;
        int shots = 0;
        for (const Size& sz : sizes) {
            c.Resize(sz.w, sz.h);
            r.Resize(sz.w, sz.h);
            for (const Page& p : Pages()) {
                c.Navigate(p.mode, p.sel, p.level, p.card);
                r.Navigate(p.mode, p.sel, p.level, p.card);
                Shot a = c.Take(), b = r.Take();
                std::wstring name = tag + L"_" + sz.name + L"_" + p.Name();
                Assert::IsFalse(a.px.empty(), (name + L": PrintWindow failed (C++)").c_str());
                Assert::IsFalse(b.px.empty(), (name + L": PrintWindow failed (Rust)").c_str());
                // The capture really shows the window: more than a flat fill,
                // and the theme's background in the top-left corner.
                Assert::IsTrue(Colors(a) >= 4, (name + L": the picture is almost one colour").c_str());
                int luma = Luma(a, 0, 0, 8, 8);
                Assert::IsTrue(dark ? luma < 100 : luma > 160, (name + L": corner luminance " + std::to_wstring(luma)).c_str());
                if (!keep.empty()) {
                    SavePng(a, keep + L"\\" + name + L"_C.png");
                    SavePng(b, keep + L"\\" + name + L"_R.png");
                }
                RECT box;
                Shot diff;
                int n = Compare(a, b, box, diff);
                shots++;
                if (n == 0) continue;
                std::wstring fd = FailDir();
                SavePng(a, fd + L"\\" + name + L"_C.png");
                SavePng(b, fd + L"\\" + name + L"_R.png");
                SavePng(diff, fd + L"\\" + name + L"_diff.png");
                if (n < 0)
                    failures += name + L": size " + std::to_wstring(a.w) + L"x" + std::to_wstring(a.h) + L" vs "
                        + std::to_wstring(b.w) + L"x" + std::to_wstring(b.h) + L"\n";
                else
                    failures += name + L": " + std::to_wstring(n) + L" pixels differ in (" + std::to_wstring(box.left) + L","
                        + std::to_wstring(box.top) + L")-(" + std::to_wstring(box.right) + L"," + std::to_wstring(box.bottom) + L")\n";
            }
        }
        Logger::WriteMessage((tag + L": " + std::to_wstring(shots) + L" pages compared\n").c_str());
        DestroyWindow(c.main); c.main = nullptr;
        DestroyWindow(r.main); r.main = nullptr;
        Pump();
        for (int m = 0; m < 16; m++) {
            DeleteFileW((dir + L"\\custom" + std::to_wstring(m) + L".txt").c_str());
            DeleteFileW((dir + L"\\custom" + std::to_wstring(m) + L".cin").c_str());
        }
        RemoveDirectoryW(dir.c_str());
        if (!failures.empty())
            Assert::Fail((L"C++ and Rust screenshots differ (pictures in " + FailDir() + L"):\n" + failures).c_str());
    }
}

namespace DIMEUnitTests
{
    TEST_CLASS(SettingsScreenshotTest)
    {
    public:
        TEST_CLASS_INITIALIZE(Start)
        {
            Gdiplus::GdiplusStartupInput input;
            Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
        }

        TEST_CLASS_CLEANUP(Cleanup)
        {
            if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
        }

        TEST_METHOD(Light96) { Run(false, 96); }
        TEST_METHOD(Light120) { Run(false, 120); }
        TEST_METHOD(Light144) { Run(false, 144); }
        TEST_METHOD(Light192) { Run(false, 192); }
        TEST_METHOD(Dark96) { Run(true, 96); }
        TEST_METHOD(Dark120) { Run(true, 120); }
        TEST_METHOD(Dark144) { Run(true, 144); }
        TEST_METHOD(Dark192) { Run(true, 192); }
    };
}

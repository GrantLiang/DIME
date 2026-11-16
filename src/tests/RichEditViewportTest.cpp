/********************************************************************************
* RichEditViewportTest: the custom table editor (自建詞庫) of the real settings
* window, C++ (SettingsWindow) vs Rust (dime-settings window.rs), both over
* the recording SettingsTestApp::FakeApp (no CConfig, no real %APPDATA%; each
* side has its own folder with the saved table). Both windows open the
* 自建詞庫 page and get the same input (messages to the RichEdit, the
* content area and the Save button); the editor state (text, selection,
* per-character colours read through TOM, event mask, Save enabled) and the
* app calls must match, and each test also checks the expected behaviour on
* its own. Replaces the bare-RICHEDIT50W version of these tests (phase 5b-12,
* docs/RUST_MIGRATION_PLAN.md; the original design is in
* docs/RICHEDIT_VIEWPORT_VAL.md, issue #130).
********************************************************************************/
#include "pch.h"
#include "CppUnitTest.h"
#include <Windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <tom.h>
#include <functional>
#include <string>
#include <vector>

#include "../DIMESettings/SettingsController.h"
#include "../DIMESettings/SettingsDriver.h"
#include "../DIMESettings/SettingsApp.h"
#include "../DIMESettings/SettingsController.h"
#include "../DIMESettings/SettingsEditor.h"
#include "dime_ffi.h"
#include "SettingsFakeApp.h"

#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace SettingsTestApp;

namespace
{
    const wchar_t kTestKey[] = L"SOFTWARE\\DIME\\SettingsRichEditTest";
    const LONG kValid = (LONG)RGB(0, 0, 0);   // CUSTOM_TABLE_LIGHT_VALID
    const int kModeIndex = 0;                 // first IME in the sidebar
    const int kCustomTableCard = 3;           // 自建詞庫 in GetIMEPageLayout

    void Pump()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

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

    std::string ReadBytes(const std::wstring& path)
    {
        std::string out;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return out;
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
        CloseHandle(h);
        return out;
    }

    std::wstring Lines(int count, const std::function<std::wstring(int)>& line)
    {
        std::wstring s;
        for (int i = 0; i < count; i++) {
            if (i) s += L"\r\n";
            s += line(i);
        }
        return s;
    }

    struct FindCtx { const wchar_t* cls; int id; HWND found; };

    BOOL CALLBACK FindProc(HWND h, LPARAM lp)
    {
        FindCtx* f = reinterpret_cast<FindCtx*>(lp);
        if (f->cls) {
            wchar_t cls[64] = {};
            GetClassNameW(h, cls, 64);
            if (_wcsicmp(cls, f->cls) == 0) { f->found = h; return FALSE; }
        } else if (GetDlgCtrlID(h) == f->id) {
            f->found = h;
            return FALSE;
        }
        return TRUE;
    }

    HWND FindChild(HWND main, const wchar_t* cls, int id = 0)
    {
        FindCtx f = { cls, id, nullptr };
        EnumChildWindows(main, FindProc, reinterpret_cast<LPARAM>(&f));
        return f.found;
    }

    CHARRANGE Sel(HWND re)
    {
        CHARRANGE cr = { -1, -1 };
        SendMessageW(re, EM_EXGETSEL, 0, (LPARAM)&cr);
        return cr;
    }

    int LineCount(HWND re) { return (int)SendMessageW(re, EM_GETLINECOUNT, 0, 0); }
    LONG LineIndex(HWND re, int line) { return (LONG)SendMessageW(re, EM_LINEINDEX, (WPARAM)line, 0); }

    // Same computation as SettingsEditor::ViewportLineRange.
    void Viewport(HWND re, int& first, int& last)
    {
        first = (int)SendMessageW(re, EM_GETFIRSTVISIBLELINE, 0, 0);
        RECT rc;
        GetClientRect(re, &rc);
        POINTL pt = { rc.right - 1, rc.bottom - 1 };
        LRESULT ch = SendMessageW(re, EM_CHARFROMPOS, 0, (LPARAM)&pt);
        last = ch >= 0 ? (int)SendMessageW(re, EM_LINEFROMCHAR, (WPARAM)ch, 0) : first;
        int n = LineCount(re);
        if (last >= n) last = n - 1;
        if (last < first) last = first;
    }

    // Foreground colour of every character of a line, through TOM.
    std::vector<LONG> LineColors(HWND re, int line)
    {
        std::vector<LONG> out;
        IUnknown* unk = nullptr;
        SendMessageW(re, EM_GETOLEINTERFACE, 0, (LPARAM)&unk);
        Assert::IsNotNull(unk, L"TOM");
        ITextDocument* doc = nullptr;
        unk->QueryInterface(__uuidof(ITextDocument), (void**)&doc);
        unk->Release();
        Assert::IsNotNull(doc, L"ITextDocument");
        LONG start = LineIndex(re, line);
        LONG len = (LONG)SendMessageW(re, EM_LINELENGTH, (WPARAM)start, 0);
        for (LONG cp = start; cp < start + len; cp++) {
            LONG c = -1;
            ITextRange* r = nullptr;
            if (SUCCEEDED(doc->Range(cp, cp + 1, &r)) && r) {
                ITextFont* f = nullptr;
                if (SUCCEEDED(r->GetFont(&f)) && f) {
                    f->GetForeColor(&c);
                    f->Release();
                }
                r->Release();
            }
            out.push_back(c);
        }
        doc->Release();
        return out;
    }

    bool AllAre(const std::vector<LONG>& v, LONG c)
    {
        for (LONG x : v) if (x != c) return false;
        return !v.empty();
    }

    bool HasErrorColor(const std::vector<LONG>& v)
    {
        for (LONG x : v) if (x != kValid && x != tomAutoColor) return true;
        return false;
    }

    std::wstring Describe(const std::vector<LONG>& v)
    {
        std::wstring s;
        for (size_t i = 0; i < v.size();) {
            size_t j = i;
            while (j < v.size() && v[j] == v[i]) j++;
            wchar_t buf[48];
            swprintf_s(buf, L"%ld*%zu ", v[i], j - i);
            s += buf;
            i = j;
        }
        return s;
    }

    std::wstring EditorText(HWND re)
    {
        int n = GetWindowTextLengthW(re);
        std::wstring s(n + 1, L'\0');
        GetWindowTextW(re, &s[0], n + 1);
        s.resize(n);
        return s;
    }

    struct Side {
        bool rust;
        FakeApp app;
        HWND main = nullptr;
        size_t logPos = 0;

        void Create()
        {
            std::wstring sfx = rust ? L"_R" : L"_C";
            std::wstring mainCls = L"DIMEReMain" + sfx, contentCls = L"DIMEReContent" + sfx;
            const wchar_t* caption = L"DIME 設定 v1.3.0";
            main = (HWND)dime_settings_window_create(static_cast<const DimeSettingsAppVtbl*>(SettingsApp::Vtbl()),
                static_cast<SettingsApp::Host*>(&app), GetModuleHandleW(nullptr), 0, (const uint16_t*)caption,
                (const uint16_t*)mainCls.c_str(), (const uint16_t*)contentCls.c_str(), (const uint16_t*)kTestKey,
                (const uint16_t*)(rust ? L"R" : L"C"), 10);
            Assert::IsNotNull(main);
            // Shown fully transparent and without activation, as in SettingsUiaTest.
            SetWindowLongW(main, GWL_EXSTYLE, GetWindowLongW(main, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
            SetLayeredWindowAttributes(main, 0, 0, LWA_ALPHA);
            SetWindowPos(main, nullptr, 50, 50, 1100, 900, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            Pump();
        }

        void Navigate(int mode, int sel, int level, int card)
        {
            dime_settings_window_navigate(main, mode, sel, level, card);
            Pump();
        }

        // New app calls, with this side's folder replaced by <dir>.
        std::wstring NewLog()
        {
            std::wstring s = app.log.substr(logPos);
            logPos = app.log.size();
            for (size_t p; (p = s.find(app.dir)) != std::wstring::npos;) s.replace(p, app.dir.size(), L"<dir>");
            return s;
        }

        HWND Editor() { return FindChild(main, L"RICHEDIT50W"); }
        HWND Content() { return GetParent(Editor()); }
        HWND SaveButton() { return FindChild(main, nullptr, 3000 + CTRL_CUSTOM_TABLE_SAVE); }
        void ClickSave()
        {
            SendMessageW(Content(), WM_COMMAND, MAKEWPARAM(3000 + CTRL_CUSTOM_TABLE_SAVE, BN_CLICKED), (LPARAM)SaveButton());
            Pump();
        }
        // Keys RichEdit handles on WM_KEYDOWN (Enter, Backspace).
        void Key(UINT vk, int times = 1)
        {
            for (int k = 0; k < times; k++) {
                SendMessageW(Editor(), WM_KEYDOWN, vk, 1);
                SendMessageW(Editor(), WM_KEYUP, vk, 0xC0000001);
            }
            Pump();
        }
        void Type(const wchar_t* chars)
        {
            for (const wchar_t* p = chars; *p; p++) SendMessageW(Editor(), WM_CHAR, *p, 1);
            Pump();
        }
    };

    // Both windows on the 自建詞庫 page of the first IME, each loading
    // `table` from its own folder.
    struct Pair {
        std::wstring base;
        Side c{ false }, r{ true };
        Side* both[2] = { &c, &r };

        explicit Pair(const std::wstring& table)
        {
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            base = std::wstring(tmp) + L"DIMERichEdit" + std::to_wstring(GetCurrentProcessId());
            CreateDirectoryW(base.c_str(), nullptr);
            SettingsSnapshot snap = SettingsModel::LoadFromConfig();
            for (Side* s : both) {
                s->app.dir = base + (s->rust ? L"\\R" : L"\\C");
                CreateDirectoryW(s->app.dir.c_str(), nullptr);
                for (int m = 0; m < 16; m++) WriteUtf16(TxtPath(*s, m), table);
                s->app.snap = snap;
                s->Create();
                s->Navigate(kModeIndex, kModeIndex, 1, kCustomTableCard);
            }
            Assert::AreEqual(c.NewLog(), r.NewLog(), L"app calls while opening the page");
            for (Side* s : both) Assert::IsNotNull(s->Editor(), s->rust ? L"Rust editor" : L"C++ editor");
        }

        ~Pair()
        {
            for (Side* s : both) {
                if (s->main) DestroyWindow(s->main);
                Pump();
                for (int m = 0; m < 16; m++) {
                    DeleteFileW(TxtPath(*s, m).c_str());
                    DeleteFileW((s->app.dir + L"\\custom" + std::to_wstring(m) + L".cin").c_str());
                }
                RemoveDirectoryW(s->app.dir.c_str());
            }
            RemoveDirectoryW(base.c_str());
        }

        static std::wstring TxtPath(Side& s, int m) { return s.app.dir + L"\\custom" + std::to_wstring(m) + L".txt"; }

        // Mode of the open page, from the path the window asked for.
        int OpenMode()
        {
            size_t p = c.app.log.find(L"txtPath ");
            Assert::IsTrue(p != std::wstring::npos, L"the editor asked for its file");
            return _wtoi(c.app.log.c_str() + p + 8);
        }

        void CompareLogs(const wchar_t* what)
        {
            std::wstring lc = c.NewLog(), lr = r.NewLog();
            if (lc != lr) Assert::Fail((std::wstring(what) + L": app calls\nC++:\n" + lc + L"Rust:\n" + lr).c_str());
        }

        // Text, selection, scroll position, event mask, Save enabled and the
        // colours of every line match.
        void CompareEditors(const wchar_t* what)
        {
            HWND a = c.Editor(), b = r.Editor();
            std::wstring w = what;
            Assert::IsTrue(EditorText(a) == EditorText(b), (w + L": text").c_str());
            CHARRANGE sa = Sel(a), sb = Sel(b);
            Assert::AreEqual(sa.cpMin, sb.cpMin, (w + L": selection start").c_str());
            Assert::AreEqual(sa.cpMax, sb.cpMax, (w + L": selection end").c_str());
            Assert::AreEqual(SendMessageW(a, EM_GETFIRSTVISIBLELINE, 0, 0), SendMessageW(b, EM_GETFIRSTVISIBLELINE, 0, 0),
                (w + L": first visible line").c_str());
            Assert::AreEqual(SendMessageW(a, EM_GETEVENTMASK, 0, 0), SendMessageW(b, EM_GETEVENTMASK, 0, 0),
                (w + L": event mask").c_str());
            Assert::AreEqual(IsWindowEnabled(c.SaveButton()), IsWindowEnabled(r.SaveButton()), (w + L": Save enabled").c_str());
            int n = LineCount(a);
            Assert::AreEqual(n, LineCount(b), (w + L": line count").c_str());
            for (int i = 0; i < n; i++) {
                std::wstring ca = Describe(LineColors(a, i)), cb = Describe(LineColors(b, i));
                if (ca != cb)
                    Assert::Fail((w + L": colours of line " + std::to_wstring(i) + L"\nC++:  " + ca + L"\nRust: " + cb).c_str());
            }
        }
    };

    LRESULT CALLBACK CountChanges(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref)
    {
        if (m == WM_COMMAND && HIWORD(w) == EN_CHANGE) ++*reinterpret_cast<int*>(ref);
        return DefSubclassProc(h, m, w, l);
    }
}

namespace DIMEIntegratedTests
{

TEST_CLASS(RichEditViewportTest)
{
public:
    TEST_CLASS_CLEANUP(Cleanup)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, kTestKey);
    }

    // IT_RE_01: a large saved table (200 K lines, issue #130) loads into the
    // editor, and typing at the end still works (EM_EXLIMITTEXT raised).
    TEST_METHOD(IT_RE_01_LargeTableAcceptsTyping)
    {
        Pair p(Lines(200000, [](int) { return std::wstring(L"a 中"); }));
        int before = GetWindowTextLengthW(p.c.Editor());
        Assert::IsTrue(before > 200000 * 3, std::to_wstring(before).c_str());
        for (Side* s : p.both) {
            HWND re = s->Editor();
            Assert::AreEqual(before, GetWindowTextLengthW(re), L"loaded length");
            SendMessageW(re, EM_SETSEL, (WPARAM)before, (LPARAM)before);
            s->Type(L"X");
            Assert::AreEqual(before + 1, GetWindowTextLengthW(re), s->rust ? L"Rust: typed at the end" : L"C++: typed at the end");
            Assert::IsTrue(IsWindowEnabled(s->SaveButton()) != FALSE, L"Save enabled after an edit");
        }
        p.CompareLogs(L"typing");
        Assert::AreEqual(LineCount(p.c.Editor()), LineCount(p.r.Editor()), L"line count");
    }

    // IT_RE_02: right after loading, the text limit is 100 MB (above the
    // RichEdit default), EN_CHANGE is on, the text is the file's, Save is
    // off, and only the visible lines are painted.
    TEST_METHOD(IT_RE_02_EditorSetup)
    {
        std::wstring table = Lines(5, [](int i) { return std::wstring(1, (wchar_t)(L'a' + i)) + L" 中"; });
        Pair p(table);
        for (Side* s : p.both) {
            HWND re = s->Editor();
            Assert::IsTrue(SendMessageW(re, EM_GETLIMITTEXT, 0, 0) >= 100LL * 1024 * 1024, L"EM_EXLIMITTEXT(100 MB)");
            Assert::AreEqual((LRESULT)ENM_CHANGE, SendMessageW(re, EM_GETEVENTMASK, 0, 0), L"ENM_CHANGE");
            Assert::AreEqual(5, LineCount(re), L"line count");
            Assert::IsFalse(IsWindowEnabled(s->SaveButton()) != FALSE, L"Save disabled until an edit");
            for (int i = 0; i < 5; i++) Assert::IsTrue(AllAre(LineColors(re, i), kValid), L"valid lines painted");
        }
        p.CompareEditors(L"after load");
    }

    // IT_RE_03: repainting (scroll, revalidate, viewport) never moves the
    // caret or the selection.
    TEST_METHOD(IT_RE_03_PaintKeepsSelection)
    {
        Pair p(Lines(300, [](int i) { return std::wstring(i % 7 == 1 ? L"foo bar baz" : L"a 中"); }));
        for (Side* s : p.both) {
            HWND re = s->Editor();
            SendMessageW(re, EM_SETSEL, 3, 8);
            PostMessageW(s->Content(), SettingsEditor::kMsgViewport, 0, 0);
            Pump();
            PostMessageW(s->Content(), SettingsEditor::kMsgRevalidate, 0, 0);
            Pump();
            SendMessageW(re, WM_VSCROLL, SB_PAGEDOWN, 0);
            Pump();
            CHARRANGE sel = Sel(re);
            Assert::AreEqual(3L, sel.cpMin, L"selection start kept");
            Assert::AreEqual(8L, sel.cpMax, L"selection end kept");
        }
        p.CompareEditors(L"after repaints");
        p.CompareLogs(L"repaints");
    }

    // IT_RE_04: only the visible lines are validated; error lines get an
    // error colour; scrolling paints the newly visible lines.
    TEST_METHOD(IT_RE_04_ViewportPaintsVisibleErrors)
    {
        const int n = 300;
        Pair p(Lines(n, [n](int i) { return std::wstring(i == 1 || i == n - 1 ? L"foo bar baz" : L"a 中"); }));
        for (Side* s : p.both) {
            HWND re = s->Editor();
            int first, last;
            Viewport(re, first, last);
            Assert::IsTrue(first == 0 && last > 1 && last < n - 1, L"viewport at the top");
            for (int i = 0; i < n; i++) {
                std::vector<LONG> v = LineColors(re, i);
                if (i > last) Assert::IsTrue(AllAre(v, tomAutoColor), (L"not painted: line " + std::to_wstring(i)).c_str());
                else if (i == 1) Assert::IsTrue(HasErrorColor(v), L"line 1 in error colour");
                else Assert::IsTrue(AllAre(v, kValid), (L"valid colour: line " + std::to_wstring(i)).c_str());
            }
        }
        p.CompareEditors(L"top");
        for (Side* s : p.both) {
            SendMessageW(s->Editor(), WM_VSCROLL, SB_BOTTOM, 0);
            Pump();
            int first, last;
            Viewport(s->Editor(), first, last);
            Assert::AreEqual(n - 1, last, L"scrolled to the end");
            Assert::IsTrue(HasErrorColor(LineColors(s->Editor(), n - 1)), L"last line painted in error colour");
        }
        p.CompareEditors(L"bottom");
        p.CompareLogs(L"scrolling");
    }

    // IT_RE_05: Save with an error at line 50 reports it and puts the caret
    // at the start of that line, scrolled into view; the file is unchanged.
    TEST_METHOD(IT_RE_05_SaveErrorJumpsToLine)
    {
        std::wstring table = Lines(60, [](int i) { return std::wstring(i == 49 ? L"foo bar baz" : L"a 中"); });
        Pair p(table);
        int mode = p.OpenMode();
        std::string original = ReadBytes(Pair::TxtPath(p.c, mode));
        for (Side* s : p.both) {
            s->ClickSave();
            HWND re = s->Editor();
            LONG cp = LineIndex(re, 49);
            CHARRANGE sel = Sel(re);
            Assert::AreEqual(cp, sel.cpMin, L"caret at line 50");
            Assert::AreEqual(cp, sel.cpMax, L"no selection");
            int first, last;
            Viewport(re, first, last);
            Assert::IsTrue(first <= 49 && 49 <= last, L"line 50 visible");
            Assert::IsTrue(original == ReadBytes(Pair::TxtPath(*s, mode)), L"file not written");
        }
        std::wstring lc = p.c.NewLog(), lr = p.r.NewLog();
        Assert::AreEqual(lc, lr, L"app calls");
        Assert::IsTrue(lc.find(L"第 50 行") != std::wstring::npos, lc.c_str());
        p.CompareEditors(L"after Save");
    }

    // IT_RE_06: painting is done with EN_CHANGE muted: no EN_CHANGE reaches
    // the content area while repainting, one per typed character, and the
    // event mask is restored.
    TEST_METHOD(IT_RE_06_ENChangeMutedDuringPaint)
    {
        Pair p(Lines(300, [](int i) { return std::wstring(i % 5 == 2 ? L"foo bar baz" : L"a 中"); }));
        for (Side* s : p.both) {
            int changes = 0;
            HWND content = s->Content(), re = s->Editor();
            Assert::IsTrue(SetWindowSubclass(content, CountChanges, 1, (DWORD_PTR)&changes) != FALSE);
            PostMessageW(content, SettingsEditor::kMsgViewport, 0, 0);
            PostMessageW(content, SettingsEditor::kMsgRevalidate, 0, 0);
            Pump();
            SendMessageW(re, WM_VSCROLL, SB_BOTTOM, 0);
            Pump();
            Assert::AreEqual(0, changes, L"no EN_CHANGE from painting");
            Assert::AreEqual((LRESULT)ENM_CHANGE, SendMessageW(re, EM_GETEVENTMASK, 0, 0), L"mask restored");
            SendMessageW(re, EM_SETSEL, 0, 0);
            s->Type(L"z");
            Assert::AreEqual(1, changes, L"one EN_CHANGE for one character");
            Assert::AreEqual((LRESULT)ENM_CHANGE, SendMessageW(re, EM_GETEVENTMASK, 0, 0), L"mask restored after typing");
            RemoveWindowSubclass(content, CountChanges, 1);
        }
        p.CompareEditors(L"after typing");
        p.CompareLogs(L"typing");
    }

    // IT_RE_07: typing repaints the edited line: a valid line that gets a
    // third token turns to an error colour, and back after Backspace.
    TEST_METHOD(IT_RE_07_TypingRepaintsLine)
    {
        Pair p(Lines(300, [](int) { return std::wstring(L"a 中"); }));
        for (Side* s : p.both) {
            HWND re = s->Editor();
            SendMessageW(re, EM_SETSEL, 0, 0);
            s->Type(L"z ");
            Assert::IsTrue(HasErrorColor(LineColors(re, 0)), L"line 0 in error colour");
            Assert::IsTrue(IsWindowEnabled(s->SaveButton()) != FALSE, L"Save enabled");
        }
        p.CompareEditors(L"after typing");
        for (Side* s : p.both) {
            s->Key(VK_BACK, 2);
            Assert::IsTrue(AllAre(LineColors(s->Editor(), 0), kValid), (std::wstring(s->rust ? L"Rust" : L"C++") + L": line 0 valid again: [" + EditorText(s->Editor()).substr(0, 12) + L"] " + Describe(LineColors(s->Editor(), 0))).c_str());
        }
        p.CompareEditors(L"after Backspace");
        p.CompareLogs(L"typing");
    }

    // IT_RE_08: Save of a valid table writes the file (UTF-16 with BOM),
    // compiles it and turns Save off.
    TEST_METHOD(IT_RE_08_SaveWritesFile)
    {
        Pair p(Lines(10, [](int i) { return std::wstring(1, (wchar_t)(L'a' + i)) + L" 中"; }));
        int mode = p.OpenMode();
        std::wstring saveLogs[2];
        for (Side* s : p.both) {
            HWND re = s->Editor();
            LONG end = GetWindowTextLengthW(re);
            SendMessageW(re, EM_SETSEL, end, end);
            s->Key(VK_RETURN);
            s->Type(L"k 字");
            Assert::IsTrue(IsWindowEnabled(s->SaveButton()) != FALSE, L"Save enabled");
            s->NewLog();
            s->ClickSave();
            std::wstring& saveLog = saveLogs[s->rust ? 1 : 0];
            saveLog = s->NewLog();
            Assert::IsFalse(IsWindowEnabled(s->SaveButton()) != FALSE, (std::wstring(s->rust ? L"Rust" : L"C++") + L": Save disabled after saving\n[" + saveLog + L"]\n" + EditorText(re).substr(0, 60)).c_str());
            std::string bytes = ReadBytes(Pair::TxtPath(*s, mode));
            Assert::IsTrue(bytes.size() > 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE, L"UTF-16 BOM");
            std::wstring saved((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / sizeof(wchar_t));
            Assert::IsTrue(saved.find(L"k 字") != std::wstring::npos, L"typed line saved");
        }
        Assert::IsTrue(ReadBytes(Pair::TxtPath(p.c, mode)) == ReadBytes(Pair::TxtPath(p.r, mode)), L"same file");
        const std::wstring& lc = saveLogs[0];
        Assert::AreEqual(lc, saveLogs[1], L"app calls of Save");
        Assert::IsTrue(lc.find(L"parse <dir>\\custom") != std::wstring::npos, lc.c_str());
        p.CompareEditors(L"after Save");
    }
};

} // namespace DIMEIntegratedTests

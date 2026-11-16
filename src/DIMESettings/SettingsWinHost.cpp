// SettingsWinHost.cpp — see SettingsWinHost.h. The Cpp functions are the
// code that used to be in WindowHost (SettingsWindow::HandleCommand);
// dime-settings (winhost.rs) mirrors them.

#include <windows.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlobj.h>
#include <strsafe.h>
#include "SettingsWinHost.h"
#include "dime_ffi.h"

namespace SettingsWinHost {

namespace Rust {

static const uint16_t* U(const wchar_t* s) { return reinterpret_cast<const uint16_t*>(s); }

int LogPixelsY(HWND wnd) { return dime_settings_win_log_pixels_y(wnd); }

bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10)
{
    return dime_settings_win_pick_font(owner, &lf, reinterpret_cast<int32_t*>(&pointSize10)) != 0;
}

bool PickColor(HWND owner, COLORREF& rgb)
{
    return dime_settings_win_pick_color(owner, reinterpret_cast<uint32_t*>(&rgb)) != 0;
}

bool OpenFileDialog(HWND owner, int filter, std::wstring& path)
{
    wchar_t buf[MAX_PATH] = {};
    if (!dime_settings_win_open_file(owner, filter, reinterpret_cast<uint16_t*>(buf))) return false;
    path = buf;
    return true;
}

bool SaveFileDialog(HWND owner, std::wstring& path)
{
    wchar_t buf[MAX_PATH] = {};
    StringCchCopyW(buf, MAX_PATH, path.c_str());
    if (!dime_settings_win_save_file(owner, reinterpret_cast<uint16_t*>(buf))) return false;
    path = buf;
    return true;
}

bool WriteBomFile(const std::wstring& path, const std::wstring& text)
{
    return dime_settings_win_write_bom_file(U(path.c_str()), U(text.c_str()), text.size()) != 0;
}

std::wstring DocumentsPath()
{
    wchar_t buf[MAX_PATH] = {};
    dime_settings_win_documents_path(reinterpret_cast<uint16_t*>(buf));
    return buf;
}

std::wstring EditorText(HWND re)
{
    size_t len = 0;
    dime_settings_win_editor_text(re, nullptr, 0, &len);
    std::wstring text(len + 1, L'\0');
    size_t got = 0;
    dime_settings_win_editor_text(re, reinterpret_cast<uint16_t*>(&text[0]), text.size(), &got);
    text.resize(len < got ? len : got);
    return text;
}

int SetEditorTextMuted(HWND re, const std::wstring& text)
{
    return dime_settings_win_set_editor_text_muted(re, U(text.c_str()));
}

int EditorLineIndex(HWND re, int line) { return dime_settings_win_editor_line_index(re, line); }
void EditorFocusAt(HWND re, int cp) { dime_settings_win_editor_focus_at(re, cp); }
int EditorLineCount(HWND re) { return dime_settings_win_editor_line_count(re); }

} // namespace Rust

namespace Impl = Rust;

int LogPixelsY(HWND wnd) { return Impl::LogPixelsY(wnd); }
bool PickFont(HWND owner, LOGFONTW& lf, int& pointSize10) { return Impl::PickFont(owner, lf, pointSize10); }
bool PickColor(HWND owner, COLORREF& rgb) { return Impl::PickColor(owner, rgb); }
bool OpenFileDialog(HWND owner, int filter, std::wstring& path) { return Impl::OpenFileDialog(owner, filter, path); }
bool SaveFileDialog(HWND owner, std::wstring& path) { return Impl::SaveFileDialog(owner, path); }
bool WriteBomFile(const std::wstring& path, const std::wstring& text) { return Impl::WriteBomFile(path, text); }
std::wstring DocumentsPath() { return Impl::DocumentsPath(); }
std::wstring EditorText(HWND re) { return Impl::EditorText(re); }
int SetEditorTextMuted(HWND re, const std::wstring& text) { return Impl::SetEditorTextMuted(re, text); }
int EditorLineIndex(HWND re, int line) { return Impl::EditorLineIndex(re, line); }
void EditorFocusAt(HWND re, int cp) { Impl::EditorFocusAt(re, cp); }
int EditorLineCount(HWND re) { return Impl::EditorLineCount(re); }

} // namespace SettingsWinHost

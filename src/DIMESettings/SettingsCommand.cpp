// SettingsCommand.cpp — see SettingsCommand.h. Cpp::Handle is the WM_COMMAND
// logic that used to live in SettingsWindow::ContentWndProc, with every side
// effect routed through Host. dime-settings (command.rs) mirrors it.

#include <windows.h>
#include <strsafe.h>
#include "..\BaseStructure.h"
#include "SettingsCommand.h"
#include "SettingsPageLayout.h"
#include "dime_ffi.h"
#include <cstddef>

namespace SettingsCommand {

static_assert(sizeof(CommandEvent) == 56, "CommandEvent must match dime-settings");

// ---- C vtable over Host for dime-settings ---------------------------------
namespace {

struct Bridge {
    Host* host;
    std::wstring keep;     // last string returned to Rust
};

Host& H(void* ctx) { return *static_cast<Bridge*>(ctx)->host; }
std::wstring W(const uint16_t* p, size_t n) { return std::wstring(reinterpret_cast<const wchar_t*>(p), n); }
const uint16_t* Keep(void* ctx, std::wstring s, size_t* len)
{
    Bridge* b = static_cast<Bridge*>(ctx);
    b->keep = std::move(s);
    if (len) *len = b->keep.size();
    return reinterpret_cast<const uint16_t*>(b->keep.c_str());
}

// Every thunk swallows exceptions: host callbacks must not throw into Rust.
#define GUARD(expr, fallback) try { expr; } catch (...) { return fallback; }

int32_t LogPixelsY(void* c) { GUARD(return H(c).LogPixelsY(), 96) }
int32_t ChooseFontT(void* c, void* lf, int32_t* pt) { GUARD({ int p = 0; bool ok = H(c).PickFont(*static_cast<LOGFONTW*>(lf), p); *pt = p; return ok ? 1 : 0; }, 0) }
int32_t ChooseColorT(void* c, uint32_t* rgb) { GUARD({ COLORREF v = *rgb; bool ok = H(c).PickColor(v); *rgb = v; return ok ? 1 : 0; }, 0) }
const uint16_t* GetOpenFileT(void* c, int32_t filter, int32_t* ok, size_t* len) { GUARD({ std::wstring p; *ok = H(c).OpenFileDialog(filter, p) ? 1 : 0; return Keep(c, p, len); }, nullptr) }
const uint16_t* GetSaveFileT(void* c, const uint16_t* def, size_t n, int32_t* ok, size_t* len) { GUARD({ std::wstring p = W(def, n); *ok = H(c).SaveFileDialog(p) ? 1 : 0; return Keep(c, p, len); }, nullptr) }
int32_t MessageBoxT(void* c, const uint16_t* t, size_t n, const uint16_t* cap, uint32_t type) { GUARD(return H(c).ShowMessage(W(t, n), reinterpret_cast<const wchar_t*>(cap), type), IDCANCEL) }
const uint16_t* AppDataPathT(void* c, size_t* len) { GUARD(return Keep(c, H(c).AppDataPath(), len), nullptr) }
const uint16_t* DocumentsPathT(void* c, size_t* len) { GUARD(return Keep(c, H(c).DocumentsPath(), len), nullptr) }
const uint16_t* TxtPathT(void* c, int32_t mode, size_t* len) { GUARD(return Keep(c, H(c).CustomTableTxtPath(mode), len), nullptr) }
const uint16_t* CinPathT(void* c, int32_t mode, size_t* len) { GUARD(return Keep(c, H(c).CustomTableCinPath(mode), len), nullptr) }
int32_t WriteBomFileT(void* c, const uint16_t* p, size_t pn, const uint16_t* t, size_t tn) { GUARD(return H(c).WriteBomFile(W(p, pn), W(t, tn)) ? 1 : 0, 0) }
const uint16_t* LoadTextFileT(void* c, const uint16_t* p, size_t pn, int32_t* ok, size_t* len) { GUARD({ std::wstring t; *ok = H(c).LoadTextFile(W(p, pn), t) ? 1 : 0; return Keep(c, t, len); }, nullptr) }
int32_t ParseCinT(void* c, const uint16_t* s, size_t sn, const uint16_t* d, size_t dn, int32_t custom) { GUARD(return H(c).ParseCin(W(s, sn), W(d, dn), custom != 0) ? 1 : 0, 0) }
void ResetAllDefaultsT(void* c, int32_t m) { try { H(c).ResetAllDefaults(m); } catch (...) {} }
void WriteConfigT(void* c, int32_t m) { try { H(c).WriteConfig(m); } catch (...) {} }
void LoadConfigT(void* c, int32_t m) { try { H(c).LoadConfig(m); } catch (...) {} }
void LoadSnapshotT(void* c, void* out) { try { *static_cast<SettingsSnapshot*>(out) = H(c).LoadSnapshot(); } catch (...) {} }
void ApplyAndSaveT(void* c) { try { H(c).ApplyAndSave(); } catch (...) {} }
void SetColorModeT(void* c, int32_t m) { try { H(c).SetColorMode(m); } catch (...) {} }
void LoadColorsForModeT(void* c, int32_t m) { try { H(c).LoadColorsForMode(m); } catch (...) {} }
void GetColorsT(void* c, uint32_t* out) { try { COLORREF v[6] = {}; H(c).GetColors(v); for (int i = 0; i < 6; i++) out[i] = v[i]; } catch (...) {} }
void SetColorT(void* c, int32_t i, uint32_t v) { try { H(c).SetColor(i, v); } catch (...) {} }
void SaveColorsForModeT(void* c, int32_t m) { try { H(c).SaveColorsForMode(m); } catch (...) {} }
void SetReverseSelT(void* c, int32_t sel) { try { H(c).SetReverseConversionSelection(sel); } catch (...) {} }
void RebuildT(void* c) { try { H(c).Rebuild(); } catch (...) {} }
void InvalidateContentT(void* c, int32_t e) { try { H(c).InvalidateContent(e != 0); } catch (...) {} }
void UpdateContentT(void* c) { try { H(c).UpdateContent(); } catch (...) {} }
void InvalidateSwatchT(void* c, int32_t i) { try { H(c).InvalidateSwatch(i); } catch (...) {} }
int32_t EditorExistsT(void* c) { GUARD(return H(c).EditorExists() ? 1 : 0, 0) }
const uint16_t* EditorTextT(void* c, size_t* len) { GUARD(return Keep(c, H(c).EditorText(), len), nullptr) }
void SetEditorTextMutedT(void* c, const uint16_t* t, size_t n) { try { H(c).SetEditorTextMuted(W(t, n)); } catch (...) {} }
int32_t EditorLineIndexT(void* c, int32_t line) { GUARD(return H(c).EditorLineIndex(line), -1) }
void EditorFocusAtT(void* c, int32_t cp) { try { H(c).EditorFocusAt(cp); } catch (...) {} }
void ValidateViewportT(void* c) { try { H(c).ValidateViewport(); } catch (...) {} }
void CancelBgScanT(void* c) { try { H(c).CancelBgScan(); } catch (...) {} }
void EnableSaveT(void* c, int32_t e) { try { H(c).EnableSave(e != 0); } catch (...) {} }
void UpdateLineCountT(void* c) { try { H(c).UpdateLineCount(); } catch (...) {} }
const uint16_t* ValidateBufferT(void* c, const uint16_t* t, size_t n, int32_t maxReport, int32_t* count, int32_t* first, size_t* len)
{
    GUARD({ int f = 0; std::wstring sum; *count = H(c).ValidateBuffer(W(t, n), maxReport, f, sum); *first = f; return Keep(c, sum, len); }, nullptr)
}
#undef GUARD

const DimeSettingsCmdHostVtbl kVtbl = {
    LogPixelsY, ChooseFontT, ChooseColorT, GetOpenFileT, GetSaveFileT, MessageBoxT,
    AppDataPathT, DocumentsPathT, TxtPathT, CinPathT, WriteBomFileT, LoadTextFileT, ParseCinT,
    ResetAllDefaultsT, WriteConfigT, LoadConfigT, LoadSnapshotT, ApplyAndSaveT,
    SetColorModeT, LoadColorsForModeT, GetColorsT, SetColorT, SaveColorsForModeT, SetReverseSelT,
    RebuildT, InvalidateContentT, UpdateContentT, InvalidateSwatchT,
    EditorExistsT, EditorTextT, SetEditorTextMutedT, EditorLineIndexT, EditorFocusAtT,
    ValidateViewportT, CancelBgScanT, EnableSaveT, UpdateLineCountT, ValidateBufferT,
};

} // namespace

void HandleRust(Host& host, SettingsSnapshot& s, int& colorGridExpanded, const CommandEvent& ev)
{
    Bridge bridge{ &host, {} };
    // The flag is written in place: Host::Rebuild reads it during the call.
    dime_settings_command_handle(&kVtbl, &bridge, &s, &colorGridExpanded, &ev);
}

void Handle(Host& host, SettingsSnapshot& s, int& colorGridExpanded, const CommandEvent& ev)
{
    HandleRust(host, s, colorGridExpanded, ev);
}

} // namespace SettingsCommand

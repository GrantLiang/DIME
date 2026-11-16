// SettingsChildren.cpp — see SettingsChildren.h. Cpp::Create is what used to
// be SettingsWindow::CreateControls with AddControl, SetupRichEditor,
// LoadRichEditor and ChildWheelSubclassProc; dime-settings (children.rs)
// mirrors it.

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <richedit.h>
#include <strsafe.h>
#include <Shlwapi.h>
#include <oleacc.h>
#include "..\BaseStructure.h"
#include "SettingsChildren.h"
#include "SettingsController.h"
#include "SettingsEditor.h"
#include "dime_ffi.h"

#pragma comment(lib, "ComCtl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "oleacc.lib")

namespace SettingsChildren {

static_assert(sizeof(DimeControlSpec) == 88, "DimeControlSpec");
static_assert(sizeof(DimeSettingsChildContext) == sizeof(Context), "DimeSettingsChildContext");

namespace {

// C thunks for DimeSettingsChildHostVtbl. Host methods must not throw; the
// try blocks keep a stray exception from unwinding into Rust.
void Th_Added(void* c, int32_t ctrlId, void* h, int32_t x, int32_t y, int32_t w, int32_t ht, int32_t visH)
{
    try { static_cast<Host*>(c)->Added(ctrlId, (HWND)h, x, y, w, ht, visH); } catch (...) {}
}

size_t Th_EditorPath(void* c, uint16_t* buf, size_t cap)
{
    try {
        std::wstring p = static_cast<Host*>(c)->EditorPath();
        if (buf && cap > 0) {
            size_t n = p.size() < cap - 1 ? p.size() : cap - 1;
            memcpy(buf, p.c_str(), n * sizeof(wchar_t));
            buf[n] = 0;
        }
        return p.size();
    } catch (...) {
        if (buf && cap > 0) buf[0] = 0;
        return 0;
    }
}

void Th_EditorReady(void* c, void* hRE, int32_t lineCount)
{
    try { static_cast<Host*>(c)->EditorReady((HWND)hRE, lineCount); } catch (...) {}
}

const DimeSettingsChildHostVtbl kVtbl = { &Th_Added, &Th_EditorPath, &Th_EditorReady };

} // namespace

void CreateRust(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs)
{
    std::vector<DimeControlSpec> raw;
    std::vector<uint16_t> text;
    for (const auto& s : specs) {
        DimeControlSpec r = {};
        r.ctrl_id = s.ctrlId;
        r.menu_id = s.menuId;
        r.role = s.role;
        r.control_class = s.controlClass;
        r.ex_style = s.exStyle;
        r.style = s.style;
        r.has_text = s.hasText ? 1 : 0;
        r.text_offset = (int32_t)text.size();
        r.text_len = s.hasText ? (int32_t)s.text.size() : 0;
        if (s.hasText)
            text.insert(text.end(), s.text.begin(), s.text.end());
        r.create[0] = s.create.left;
        r.create[1] = s.create.top;
        r.create[2] = s.create.right;
        r.create[3] = s.create.bottom;
        r.add[0] = s.add.left;
        r.add[1] = s.add.top;
        r.add[2] = s.add.right;
        r.add[3] = s.add.bottom;
        r.vis_h = s.visH;
        r.set_font = s.setFont ? 1 : 0;
        r.dark_theme = s.darkTheme;
        r.acc_offset = (int32_t)text.size();
        r.acc_len = (int32_t)s.accName.size();
        text.insert(text.end(), s.accName.begin(), s.accName.end());
        raw.push_back(r);
    }
    dime_settings_create_children(&kVtbl, &host, &ctx, raw.data(), raw.size(), text.data(), text.size());
}

void Create(Host& host, const Context& ctx, const std::vector<SettingsControls::ControlSpec>& specs)
{
    CreateRust(host, ctx, specs);
}

} // namespace SettingsChildren

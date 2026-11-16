// SettingsEditor.cpp — see SettingsEditor.h. The Cpp:: code is the editor
// logic that used to live in SettingsWindow.cpp; dime-settings (editor.rs)
// mirrors it.

#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <tom.h>
#include <vector>
#include "..\Define.h"
#include "..\BaseStructure.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsEditor.h"
#include "SettingsController.h"
#include "dime_ffi.h"
#pragma comment(lib, "comctl32.lib")

namespace SettingsEditor {

namespace {
extern "C" int32_t SettingsEditorCharOk(uint16_t ch, void* ctx)
{
    CCompositionProcessorEngine* engine = static_cast<CCompositionProcessorEngine*>(ctx);
    if (engine == nullptr) return 0;
    return engine->ValidateCompositionKeyCharFull(static_cast<WCHAR>(ch)) ? 1 : 0;
}
}

void ViewportLineRange(HWND hRE, int* firstVis, int* lastVis)
{
    int f = 0, l = 0;
    dime_settings_editor_viewport(hRE, &f, &l);
    if (firstVis) *firstVis = f;
    if (lastVis) *lastVis = l;
}

void ValidateViewport(HWND hRE, const EditorContext& ctx)
{
    dime_settings_editor_validate_viewport(hRE, ctx.mode, ctx.maxCodes, ctx.isDark,
        ctx.engine ? &SettingsEditorCharOk : nullptr, ctx.engine);
}

SUBCLASSPROC SubclassProc()
{
    return reinterpret_cast<SUBCLASSPROC>(dime_settings_editor_subclass_proc());
}

} // namespace SettingsEditor

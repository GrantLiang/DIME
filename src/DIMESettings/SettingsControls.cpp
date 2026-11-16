// SettingsControls.cpp — see SettingsControls.h. Cpp::BuildSpecs follows the
// code that used to be CreateRowControl / CreateColorGridControls /
// CreateRichEditorControl; dime-settings (controls.rs) mirrors it.

#include <windows.h>
#include <richedit.h>
#include "..\BaseStructure.h"
#include "SettingsControls.h"
#include "SettingsGeometry.h"
#include "SettingsPageLayout.h"
#include "dime_ffi.h"

namespace SettingsControls {

const wchar_t* ClassName(int controlClass)
{
    switch (controlClass) {
    case CLS_COMBOBOX: return L"COMBOBOX";
    case CLS_EDIT:     return L"EDIT";
    case CLS_STATIC:   return L"STATIC";
    case CLS_RICHEDIT: return L"RICHEDIT50W";
    default:           return L"BUTTON";
    }
}

static_assert(sizeof(DimeControlSpec) == 88, "DimeControlSpec");

std::vector<ControlSpec> BuildSpecsRust(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount)
{
    size_t count = 0, textLen = 0;
    dime_settings_control_specs(&in, cardW, ime, imeCount, load, loadCount, nullptr, 0, &count, nullptr, 0, &textLen);
    std::vector<DimeControlSpec> raw(count);
    std::vector<uint16_t> text(textLen + 1);
    dime_settings_control_specs(&in, cardW, ime, imeCount, load, loadCount, raw.data(), raw.size(), &count,
        text.data(), text.size(), &textLen);
    std::vector<ControlSpec> out;
    for (const DimeControlSpec& r : raw) {
        ControlSpec s = {};
        s.ctrlId = r.ctrl_id;
        s.menuId = r.menu_id;
        s.role = r.role;
        s.controlClass = r.control_class;
        s.exStyle = r.ex_style;
        s.style = r.style;
        s.hasText = r.has_text != 0;
        if (r.text_len > 0 && (size_t)r.text_offset + (size_t)r.text_len <= text.size())
            s.text.assign(reinterpret_cast<const wchar_t*>(text.data()) + r.text_offset, (size_t)r.text_len);
        s.create = { r.create[0], r.create[1], r.create[2], r.create[3] };
        s.add = { r.add[0], r.add[1], r.add[2], r.add[3] };
        s.visH = r.vis_h;
        s.setFont = r.set_font != 0;
        s.darkTheme = r.dark_theme;
        if (r.acc_len > 0 && (size_t)r.acc_offset + (size_t)r.acc_len <= text.size())
            s.accName.assign(reinterpret_cast<const wchar_t*>(text.data()) + r.acc_offset, (size_t)r.acc_len);
        out.push_back(s);
    }
    return out;
}

std::vector<ControlSpec> BuildSpecs(const SettingsWindowLayout::PlanInput& in, int cardW,
    const LayoutCard* ime, int imeCount, const LayoutCard* load, int loadCount)
{
    return BuildSpecsRust(in, cardW, ime, imeCount, load, loadCount);
}

} // namespace SettingsControls

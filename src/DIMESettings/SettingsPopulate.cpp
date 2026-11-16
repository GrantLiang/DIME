// SettingsPopulate.cpp — see SettingsPopulate.h. Cpp::Populate is the code
// that used to be SettingsWindow::PopulateControls; dime-settings
// (populate.rs) produces the same control operations.

#include <windows.h>
#include <strsafe.h>
#include <string>
#include "..\Define.h"
#include "..\BaseStructure.h"
#include "SettingsPopulate.h"
#include "dime_ffi.h"

namespace SettingsPopulate {

static_assert(sizeof(DimePopulateOp) == 28, "DimePopulateOp");

void PopulateRust(const Inputs& in, FindControlFn find, void* ctx)
{
    std::vector<const uint16_t*> descs;
    std::vector<int32_t> selected;
    for (int i = 0; i < in.reverseCount; i++) {
        descs.push_back(reinterpret_cast<const uint16_t*>(in.reverse[i].description));
        selected.push_back(in.reverse[i].selected ? 1 : 0);
    }
    size_t opCount = 0, textLen = 0;
    dime_settings_populate_ops(in.snapshot, in.windows1809OrLater ? 1 : 0, in.reverseIsNull ? 1 : 0,
        descs.data(), selected.data(), (int32_t)descs.size(), nullptr, 0, &opCount, nullptr, 0, &textLen);
    std::vector<DimePopulateOp> ops(opCount);
    std::vector<uint16_t> text(textLen + 1);
    dime_settings_populate_ops(in.snapshot, in.windows1809OrLater ? 1 : 0, in.reverseIsNull ? 1 : 0,
        descs.data(), selected.data(), (int32_t)descs.size(), ops.data(), ops.size(), &opCount,
        text.data(), text.size(), &textLen);

    for (const DimePopulateOp& op : ops) {
        HWND h = find(ctx, (SettingsControlId)op.ctrl_id);
        if (!h) continue;
        std::wstring str;
        if (op.text_len > 0 && (size_t)op.text_offset + (size_t)op.text_len <= text.size())
            str.assign(reinterpret_cast<const wchar_t*>(text.data()) + op.text_offset, (size_t)op.text_len);
        switch (op.kind) {
        case DIME_POPULATE_RESET:
            SendMessage(h, CB_RESETCONTENT, 0, 0);
            break;
        case DIME_POPULATE_ADD: {
            LRESULT idx = SendMessage(h, CB_ADDSTRING, 0, (LPARAM)str.c_str());
            if (op.has_data) SendMessage(h, CB_SETITEMDATA, (WPARAM)idx, (LPARAM)op.data);
            break;
        }
        case DIME_POPULATE_SELECT:
            SendMessage(h, CB_SETCURSEL, (WPARAM)op.value, 0);
            break;
        case DIME_POPULATE_CHECK:
            SendMessage(h, BM_SETCHECK, op.value ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case DIME_POPULATE_TEXT:
            SetWindowTextW(h, str.c_str());
            break;
        default:
            break;
        }
    }
}

void Populate(const Inputs& in, FindControlFn find, void* ctx) { PopulateRust(in, find, ctx); }

} // namespace SettingsPopulate

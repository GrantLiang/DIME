// SettingsWindowLayout.cpp — window-free computations of SettingsWindow.
// See SettingsWindowLayout.h. The Cpp:: functions are the code that used to
// live inline in SettingsWindow.cpp; dime-settings (Rust) mirrors them.

#include <windows.h>
#include <strsafe.h>
#include "..\BaseStructure.h"
#include "..\CompositionProcessorEngine.h"
#include "SettingsWindowLayout.h"
#include "SettingsGeometry.h"
#include "dime_ffi.h"
#include <cstddef>

namespace SettingsWindowLayout {

static_assert(sizeof(RowContext) == 24, "RowContext must match dime-settings");
static_assert(sizeof(PlanInput) == 48, "PlanInput must match dime-settings");
static_assert(sizeof(Placement) == 20, "Placement must match dime-settings");
static_assert(sizeof(PlanResult) == 12, "PlanResult must match dime-settings");
static_assert(sizeof(CtrlRect) == 20, "CtrlRect must match dime-settings");
static_assert(sizeof(MsgBoxButtons) == 28, "MsgBoxButtons must match dime-settings");
static_assert(sizeof(MsgBoxLayout) == 64, "MsgBoxLayout must match dime-settings");
static_assert(sizeof(bool) == 1, "PlanInput::expanded is one byte per slot");

// ============================================================================
// Dispatch
// ============================================================================

static_assert(offsetof(PlanInput, expanded) == 32, "PlanInput::expanded");
static_assert(offsetof(MsgBoxLayout, margin) == 60, "MsgBoxLayout::margin");

namespace {
extern "C" int32_t SettingsWindowLayoutCharOk(uint16_t ch, void* ctx)
{
    CCompositionProcessorEngine* engine = static_cast<CCompositionProcessorEngine*>(ctx);
    if (engine == nullptr) return 0;
    return engine->ValidateCompositionKeyCharFull(static_cast<WCHAR>(ch)) ? 1 : 0;
}
}

int CardWidth(int clientRight, UINT dpi, bool sidebarTakesSpace, int vscrollW)
{
    return dime_settings_card_width(clientRight, dpi, sidebarTakesSpace ? 1 : 0, vscrollW);
}

int CtrlRight(int margin, int cardW, UINT dpi, bool hasChevron)
{
    return dime_settings_ctrl_right(margin, cardW, dpi, hasChevron ? 1 : 0);
}

int RowHeight(const LayoutRow& lr, UINT dpi, int previewH)
{
    return dime_settings_row_height(&lr, dpi, previewH);
}

bool RowShown(const LayoutRow& lr, int modeMask, int colorMode, bool gridExpanded)
{
    return dime_settings_row_shown(&lr, modeMask, colorMode, gridExpanded ? 1 : 0) != 0;
}

int CardListHitTest(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    return dime_settings_card_list_hit_test(y, cards, cardCount, modeMask,
        reinterpret_cast<const uint8_t*>(cardExpanded), expandBaseIdx, dpi, scrollPos);
}

CardListClick CardListClickTarget(int y, const LayoutCard* cards, int cardCount, int modeMask,
    const bool* cardExpanded, int expandBaseIdx, UINT dpi, int scrollPos)
{
    CardListClick c = { 0, -1, -1 };
    dime_settings_card_list_click(y, cards, cardCount, modeMask,
        reinterpret_cast<const uint8_t*>(cardExpanded), expandBaseIdx, dpi, scrollPos, &c);
    return c;
}

std::vector<RowSpan> RowSpans(const LayoutCard& card, const RowContext& ctx)
{
    size_t count = 0;
    dime_settings_row_spans(&card, &ctx, nullptr, 0, &count);
    std::vector<RowSpan> spans(count);
    if (count > 0) {
        size_t written = 0;
        dime_settings_row_spans(&card, &ctx, spans.data(), spans.size(), &written);
        if (written < spans.size()) spans.resize(written);
    }
    return spans;
}

RowCardHit RowCardsHit(int y, const LayoutCard& card, const RowContext& ctx)
{
    RowCardHit h = { -1, -1 };
    dime_settings_row_cards_hit(y, &card, &ctx, &h);
    return h;
}

PlanResult PlanControls(const PlanInput& in, const LayoutCard* ime, int imeCount,
    const LayoutCard* load, int loadCount, std::vector<Placement>& out)
{
    PlanResult res = { 0, 0, 0 };
    size_t count = 0;
    dime_settings_plan_controls(&in, ime, imeCount, load, loadCount, nullptr, 0, &count, &res);
    out.assign(count, Placement{});
    if (count > 0) {
        size_t written = 0;
        dime_settings_plan_controls(&in, ime, imeCount, load, loadCount, out.data(), out.size(), &written, &res);
        if (written < out.size()) out.resize(written);
    }
    return res;
}

bool RowControlRect(RowType type, int ctrlRight, int cy, int ctrlH, int btnH, UINT dpi, CtrlRect* out)
{
    return dime_settings_row_control_rect(static_cast<int32_t>(type), ctrlRight, cy, ctrlH, btnH, dpi, out) != 0;
}

CtrlRect SwatchRect(int index, int margin, int curY, int ctrlRight, UINT dpi)
{
    CtrlRect r = {};
    dime_settings_swatch_rect(index, margin, curY, ctrlRight, dpi, &r);
    return r;
}

void RichEditorRects(int margin, int cardW, int pad, int curY, int thisH, int ctrlRight,
    int btnW, int btnH, UINT dpi, CtrlRect* editor, CtrlRect* save)
{
    dime_settings_rich_editor_rects(margin, cardW, pad, curY, thisH, ctrlRight, btnW, btnH, dpi, editor, save);
}

CtrlRect ContentAreaRect(int clientW, int clientH, UINT dpi, bool sidebarTakesSpace, bool hasBreadcrumb)
{
    CtrlRect r = {};
    dime_settings_content_area_rect(clientW, clientH, dpi, sidebarTakesSpace ? 1 : 0, hasBreadcrumb ? 1 : 0, &r);
    return r;
}

int ValidateBuffer(const wchar_t* text, size_t wlen, IME_MODE mode, UINT maxCodes,
    CCompositionProcessorEngine* pEng, int* outFirstErrLine, wchar_t* outSummary,
    size_t summaryCch, int maxReport)
{
    return dime_settings_validate_buffer(reinterpret_cast<const uint16_t*>(text), wlen,
        static_cast<int32_t>(mode), maxCodes,
        pEng ? &SettingsWindowLayoutCharOk : nullptr, pEng, maxReport,
        outFirstErrLine, reinterpret_cast<uint16_t*>(outSummary), summaryCch);
}

MsgBoxButtons MsgBoxButtonsFor(UINT type)
{
    MsgBoxButtons b = {};
    dime_settings_msgbox_buttons(type, &b);
    return b;
}

int MsgBoxIcon(UINT type) { return dime_settings_msgbox_icon(type); }

int MsgBoxMaxTextWidth(UINT dpi) { return dime_settings_msgbox_max_text_width(dpi); }

MsgBoxLayout MsgBoxLayoutFor(bool hasIcon, int btnCount, int textW, int textH, UINT dpi)
{
    MsgBoxLayout L = {};
    dime_settings_msgbox_layout(hasIcon ? 1 : 0, btnCount, textW, textH, dpi, &L);
    return L;
}

int MsgBoxFirstButtonX(const MsgBoxLayout& L, int btnCount)
{
    return dime_settings_msgbox_first_button_x(&L, btnCount);
}

int MsgBoxButtonAt(const MsgBoxLayout& L, int btnCount, int x, int y)
{
    return dime_settings_msgbox_button_at(&L, btnCount, x, y);
}

int MsgBoxKeyResult(const MsgBoxButtons& b, int defaultBtn, UINT vk)
{
    return dime_settings_msgbox_key_result(&b, defaultBtn, vk);
}

int MsgBoxCloseResult(const MsgBoxButtons& b)
{
    return dime_settings_msgbox_close_result(&b);
}

} // namespace SettingsWindowLayout

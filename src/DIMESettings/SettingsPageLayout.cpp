// SettingsPageLayout.cpp — THE layout definition
//
// This file defines the card→row tree structure for all pages.
// To add a new setting: add to ControlDef[] (SettingsModel.cpp) + add one line here.
// To rearrange: reorder lines here. Zero C++ renderer changes.

#include <windows.h>
#include <cstddef>
#include "SettingsPageLayout.h"
#include "SettingsController.h"
#include "..\resource.h"
#include "dime_ffi.h"

// LayoutRow / LayoutCard / SidebarItemDef must match dime-config::page_layout.
#ifdef _WIN64
static_assert(sizeof(LayoutRow) == 48, "LayoutRow size");
static_assert(alignof(LayoutRow) == 8, "LayoutRow align");
static_assert(offsetof(LayoutRow, label) == 24, "LayoutRow label");
static_assert(offsetof(LayoutRow, visibleModes) == 40, "LayoutRow visibleModes");
static_assert(offsetof(LayoutRow, iconGlyph) == 46, "LayoutRow iconGlyph");
static_assert(sizeof(LayoutCard) == 48, "LayoutCard size");
static_assert(alignof(LayoutCard) == 8, "LayoutCard align");
static_assert(offsetof(LayoutCard, rows) == 24, "LayoutCard rows");
static_assert(offsetof(LayoutCard, defaultExpanded) == 42, "LayoutCard defaultExpanded");
static_assert(sizeof(SidebarItemDef) == 16, "SidebarItemDef size");
static_assert(offsetof(SidebarItemDef, iconResourceId) == 8, "SidebarItemDef icon");
#else
static_assert(sizeof(LayoutRow) == 36, "LayoutRow size");
static_assert(alignof(LayoutRow) == 4, "LayoutRow align");
static_assert(offsetof(LayoutRow, label) == 20, "LayoutRow label");
static_assert(offsetof(LayoutRow, visibleModes) == 28, "LayoutRow visibleModes");
static_assert(offsetof(LayoutRow, iconGlyph) == 34, "LayoutRow iconGlyph");
static_assert(sizeof(LayoutCard) == 28, "LayoutCard size");
static_assert(alignof(LayoutCard) == 4, "LayoutCard align");
static_assert(offsetof(LayoutCard, rows) == 12, "LayoutCard rows");
static_assert(offsetof(LayoutCard, defaultExpanded) == 26, "LayoutCard defaultExpanded");
static_assert(sizeof(SidebarItemDef) == 8, "SidebarItemDef size");
static_assert(offsetof(SidebarItemDef, iconResourceId) == 4, "SidebarItemDef icon");
#endif
static_assert(static_cast<int>(RowType::Toggle) == 0, "RowType::Toggle");
static_assert(static_cast<int>(RowType::ComboBox) == 1, "RowType::ComboBox");
static_assert(static_cast<int>(RowType::Edit) == 2, "RowType::Edit");
static_assert(static_cast<int>(RowType::ColorGrid) == 3, "RowType::ColorGrid");
static_assert(static_cast<int>(RowType::Clickable) == 4, "RowType::Clickable");
static_assert(static_cast<int>(RowType::Button) == 5, "RowType::Button");
static_assert(static_cast<int>(RowType::RichEditor) == 6, "RowType::RichEditor");
static_assert(static_cast<int>(RowType::CandidatePreview) == 7, "RowType::CandidatePreview");
static_assert(static_cast<int>(RowAction::None) == 0, "RowAction::None");
static_assert(static_cast<int>(RowAction::ToggleValue) == 1, "RowAction::ToggleValue");
static_assert(static_cast<int>(RowAction::OpenFontDialog) == 2, "RowAction::OpenFontDialog");
static_assert(static_cast<int>(RowAction::OpenColorDialog) == 3, "RowAction::OpenColorDialog");
static_assert(static_cast<int>(RowAction::ExpandSection) == 4, "RowAction::ExpandSection");
static_assert(static_cast<int>(RowAction::NavigateToCard) == 5, "RowAction::NavigateToCard");
static_assert(static_cast<int>(RowAction::LoadTable) == 6, "RowAction::LoadTable");
static_assert(static_cast<int>(RowAction::SaveCustomTable) == 7, "RowAction::SaveCustomTable");
static_assert(static_cast<int>(RowAction::ExportCustomTable) == 8, "RowAction::ExportCustomTable");
static_assert(static_cast<int>(RowAction::ImportCustomTable) == 9, "RowAction::ImportCustomTable");
static_assert(static_cast<int>(RowAction::ResetDefaults) == 10, "RowAction::ResetDefaults");
static_assert(CTRL_NONE == 0, "CTRL_NONE");
static_assert(CTRL_FONT_NAME == 1, "CTRL_FONT_NAME");
static_assert(CTRL_COLOR_MODE == 4, "CTRL_COLOR_MODE");
static_assert(CTRL_RESTORE_DEFAULT == 12, "CTRL_RESTORE_DEFAULT");
static_assert(CTRL_ARRAY_SCOPE == 13, "CTRL_ARRAY_SCOPE");
static_assert(CTRL_MAX_CODES == 18, "CTRL_MAX_CODES");
static_assert(CTRL_REVERSE_CONVERSION == 33, "CTRL_REVERSE_CONVERSION");
static_assert(CTRL_COLOR_GRID == 41, "CTRL_COLOR_GRID");
static_assert(CTRL_CANDIDATE_PREVIEW == 42, "CTRL_CANDIDATE_PREVIEW");
static_assert(CTRL_DAYI_ARTICLE_CHARS == 43, "CTRL_DAYI_ARTICLE_CHARS");
static_assert(CTRL_CUSTOM_TABLE_EDITOR == 200, "CTRL_CUSTOM_TABLE_EDITOR");
static_assert(CTRL_CUSTOM_TABLE_EXPORT == 202, "CTRL_CUSTOM_TABLE_EXPORT");
static_assert(CTRL_CUSTOM_TABLE_IMPORT == 203, "CTRL_CUSTOM_TABLE_IMPORT");
static_assert(IDI_DIMESETTINGS == 10, "IDI_DIMESETTINGS");
static_assert(IDI_DAYI == 11, "IDI_DAYI");
static_assert(IDI_ARRAY == 12, "IDI_ARRAY");
static_assert(IDI_PHONETIC == 13, "IDI_PHONETIC");
static_assert(IDI_GENERIC == 14, "IDI_GENERIC");
static_assert(SM_MODE_ALL == 0xF, "SM_MODE_ALL");
static_assert(SM_MODE_NON_ARRAY == (SM_MODE_DAYI | SM_MODE_PHONETIC | SM_MODE_GENERIC), "SM_MODE_NON_ARRAY");

// ============================================================================
// Public accessors
// ============================================================================

const LayoutCard* GetIMEPageLayout(int* count)
{
    return reinterpret_cast<const LayoutCard*>(
        dime_settings_ime_page_layout(reinterpret_cast<int32_t*>(count)));
}

const LayoutCard* GetLoadTablePageLayout(int* count)
{
    return reinterpret_cast<const LayoutCard*>(
        dime_settings_load_table_page_layout(reinterpret_cast<int32_t*>(count)));
}

const SidebarItemDef* GetSidebarItems(int* count)
{
    return reinterpret_cast<const SidebarItemDef*>(
        dime_settings_sidebar_items(reinterpret_cast<int32_t*>(count)));
}

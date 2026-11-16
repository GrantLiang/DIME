// UIWindowTypes.h - the callback types of the candidate and notify windows
// (from the C++ CCandidateWindow / CNotifyWindow, removed in phase 6b-3; the
// windows are dime-ui's now, and UIPresenter passes these callbacks to them).
#pragma once

#include "Private.h"
#include "BaseStructure.h"
#include "Define.h"
#include "CandidateListModel.h"

enum class CANDWND_ACTION
{
    CAND_ITEM_SELECT,
    CAND_CANCEL
};

typedef HRESULT (*CANDWNDCALLBACK)(void *pv, enum CANDWND_ACTION action);

enum class NOTIFY_WND
{
    SWITCH_CHN_ENG,
    SHOW_NOTIFY
};

typedef HRESULT (*NOTIFYWNDCALLBACK)(void *pv, enum NOTIFY_WND action, _In_ WPARAM wParam, _In_ LPARAM lParam);

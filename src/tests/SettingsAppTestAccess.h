// SettingsAppTestAccess.h - resets and reads the CConfig statics the settings
// app's configuration keeps (SettingsAppParityTest, CliHostParityTest).
#pragma once

#include <Windows.h>
#include "../Globals.h"
#include "../Config.h"
#include "../Define.h"

namespace DIMEUnitTests
{
    // Friend of CConfig: its statics for the settings app.
    struct SettingsAppConfigAccess
    {
        struct Saved {
            IME_MODE imeMode, configMode;
            struct _stat stamp;
            WCHAR ini[MAX_PATH], profile[MAX_PATH];
            COLORREF colors[6];
        };

        static Saved Save()
        {
            Saved s = {};
            s.imeMode = CConfig::_imeMode;
            s.configMode = CConfig::_configIMEMode;
            s.stamp = CConfig::_initTimeStamp;
            memcpy(s.ini, CConfig::_pwszINIFileName, sizeof(s.ini));
            memcpy(s.profile, CConfig::_pwzsDIMEProfile, sizeof(s.profile));
            for (int i = 0; i < 6; i++) s.colors[i] = CConfig::colors[i].color;
            return s;
        }

        static void Restore(const Saved& s)
        {
            CConfig::_imeMode = s.imeMode;
            CConfig::_configIMEMode = s.configMode;
            CConfig::_initTimeStamp = s.stamp;
            memcpy(CConfig::_pwszINIFileName, s.ini, sizeof(s.ini));
            memcpy(CConfig::_pwzsDIMEProfile, s.profile, sizeof(s.profile));
            for (int i = 0; i < 6; i++) CConfig::colors[i].color = s.colors[i];
        }

        // The static initialisers.
        static void Reset()
        {
            CConfig::_imeMode = IME_MODE::IME_MODE_NONE;
            CConfig::_configIMEMode = IME_MODE::IME_MODE_NONE;
            CConfig::_initTimeStamp = {};
            CConfig::_pwszINIFileName[0] = 0;
            CConfig::_pwzsDIMEProfile[0] = 0;
            const COLORREF init[6] = { CANDWND_ITEM_COLOR, CANDWND_SELECTED_ITEM_COLOR, GetSysColor(COLOR_3DHIGHLIGHT),
                CANDWND_PHRASE_COLOR, CANDWND_NUM_COLOR, CANDWND_SELECTED_BK_COLOR };
            for (int i = 0; i < 6; i++) CConfig::colors[i].color = init[i];
        }
    };
}

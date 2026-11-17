// TestStubs.cpp - the DllMain.cpp symbols the product code uses, for the tests
// (DIMETests compiles the product sources but not DllMain.cpp).

#include "pch.h"
#include "DIME.h"
#include "Globals.h"

// DLL reference counting (DllMain.cpp).
void DllAddRef(void)
{
}

void DllRelease(void)
{
}

namespace Global {
    // Defined in DllMain.cpp (set at DLL_PROCESS_ATTACH).
    DWORD g_WinBuildNumber = 0;
}

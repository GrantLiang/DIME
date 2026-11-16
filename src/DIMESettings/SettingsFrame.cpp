// SettingsFrame.cpp — see SettingsFrame.h. The logic is dime-settings
// frame.rs (the C++ original was removed in phase 6a-4); this forwards to it.

#include <windows.h>
#include "..\BaseStructure.h"
#include "SettingsFrame.h"
#include "SettingsGeometry.h"
#include "dime_ffi.h"

namespace SettingsFrame {

static_assert(sizeof(FrameState) == 4, "FrameState must match dime-settings");
static_assert(sizeof(FrameEvent) == 48, "FrameEvent must match dime-settings");
static_assert(sizeof(FrameEffect) == 24, "FrameEffect must match dime-settings");

namespace Rust {

void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out)
{
    FrameEffect buf[32];
    size_t count = 0;
    dime_settings_frame_handle(&s, &ev, buf, _countof(buf), &count);
    for (size_t i = 0; i < count && i < _countof(buf); i++)
        out.push_back(buf[i]);
}

SIZE InitialSize(UINT dpi)
{
    int32_t sz[2] = {};
    dime_settings_frame_initial_size(dpi, sz);
    return { sz[0], sz[1] };
}

} // namespace Rust

namespace Impl = Rust;

void Handle(FrameState& s, const FrameEvent& ev, std::vector<FrameEffect>& out) { Impl::Handle(s, ev, out); }
SIZE InitialSize(UINT dpi) { return Impl::InitialSize(dpi); }

} // namespace SettingsFrame

#pragma once
#include <cstddef>
#include <cstdint>
namespace buzz_usb {
struct ScriptEvent { double second; uint32_t mask; bool fired = false; };
// Explicit test input only: retain a due press until the USB report thread
// can consume it, rather than requiring a poll inside a 200 ms wall-time slot.
inline uint32_t due_script_pulses(ScriptEvent* events, size_t count, double second)
{
    uint32_t buttons = 0;
    for (size_t n=0;n<count;++n) if (!events[n].fired && second>=events[n].second) {
        events[n].fired=true;
        buttons |= events[n].mask & 0xfffffu;
    }
    return buttons;
}
}

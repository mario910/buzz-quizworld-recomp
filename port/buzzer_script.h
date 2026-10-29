#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstring>
namespace buzz_usb {
struct ScriptEvent { double second; uint32_t mask; bool fired = false; };
// Explicit test mailbox: monotonic sequence + twenty handset bits. A partial,
// duplicated or stale write must not create another press. No OS key injection.
struct TestMailbox {
    unsigned long long seen=0;
    uint32_t consume(const char* text) {
        unsigned long long sequence=0;unsigned mask=0;int end=0;
        if(!text || !std::isdigit((unsigned char)text[0]) || !std::strchr(text,'\n') ||
            std::sscanf(text,"%llu:%x%n",&sequence,&mask,&end)!=2)return 0;
        while(text[end] && std::isspace((unsigned char)text[end]))++end;
        if(text[end] || sequence<=seen || mask>0xfffffu)return 0;
        seen=sequence;return mask;
    }
};
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

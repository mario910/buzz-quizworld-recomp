#include "../port/buzzer_script.h"
#include <cassert>
#include <cstdio>
int main()
{
    buzz_usb::ScriptEvent events[]={{1,1},{3,4},{5,2}};
    assert(buzz_usb::due_script_pulses(events,3,0)==0);
    assert(buzz_usb::due_script_pulses(events,3,2.5)==1); // 200 ms window missed
    assert(buzz_usb::due_script_pulses(events,3,2.6)==0);
    assert(buzz_usb::due_script_pulses(events,3,3)==4);
    assert(buzz_usb::due_script_pulses(events,3,20)==2);
    assert(buzz_usb::due_script_pulses(events,3,21)==0);
    assert(buzz_usb::due_script_pulses(events,0,100)==0);
    buzz_usb::ScriptEvent players[]={{1,0x20},{2,0x400},{3,0x8000},{4,0xffffffffu}};
    assert(buzz_usb::due_script_pulses(players,4,3)==0x8420u);
    assert(buzz_usb::due_script_pulses(players,4,3.5)==0);
    assert(buzz_usb::due_script_pulses(players,4,4)==0xfffffu);
    assert(buzz_usb::due_script_pulses(players,4,100)==0);
    buzz_usb::TestMailbox mailbox;
    assert(mailbox.consume("1:1\n")==1); // P1 red
    assert(mailbox.consume("1:1")==0);
    assert(mailbox.consume("2:20\n")==0x20); // P2 red
    assert(mailbox.consume("3:21\n")==0x21); // simultaneous P1/P2
    assert(mailbox.consume("2:20")==0);
    assert(mailbox.consume("4:")==0);
    assert(mailbox.consume("4:100000")==0);
    assert(mailbox.consume("4:1 garbage")==0);
    assert(mailbox.consume("4:2")==0); // partial write without a terminator
    assert(mailbox.consume("-1:1\n")==0);
    assert(mailbox.consume("4:0\n")==0 && mailbox.seen==4);
    puts("Buzzer script: late delivery, exactly once, ordered events, empty script PASS");
}

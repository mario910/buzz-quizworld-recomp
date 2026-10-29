#include "../ps3recomp/libs/video/rsx_buffer_budget.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(rsx_parse_budget(NULL, 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("8192", 2048, 64, 8192) == 8192);
    assert(rsx_parse_budget("64", 2048, 64, 8192) == 64);
    assert(rsx_parse_budget("8193", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("63", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("-1", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("0", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("8192junk", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("9999999999999999999999999", 2048, 64, 8192) == 2048);
    assert(rsx_parse_budget("1048576", 393216, 16384, 2097152) == 1048576);
    assert(rsx_parse_budget(NULL,96,16,512)==96);
    assert(rsx_parse_budget("256",96,16,512)==256);
    assert(rsx_parse_budget("512",96,16,512)==512);
    assert(rsx_parse_budget("513",96,16,512)==96);
    puts("PASS: 15 buffer-budget validation cases");
}

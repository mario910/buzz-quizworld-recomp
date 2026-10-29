#include "../port/boot_probe_options.h"
#include <cassert>
#include <cstdio>
int main(){
    assert(!buzz_boot::enabled(nullptr) && !buzz_boot::enabled("") && !buzz_boot::enabled("0"));
    assert(buzz_boot::enabled("1"));
    unsigned hooks=0;
    for(unsigned i=0;i<19;++i){
        assert(!buzz_boot::needs_probe(i,false,false));
        assert(buzz_boot::needs_probe(i,true,false));
        assert(buzz_boot::needs_probe(i,true,true));
        hooks+=buzz_boot::needs_probe(i,false,true);
    }
    assert(hooks==5 && buzz_boot::needs_probe(17,false,true) && buzz_boot::needs_probe(5,false,true) && buzz_boot::needs_probe(6,false,true) &&
        buzz_boot::needs_probe(7,false,true) && buzz_boot::needs_probe(10,false,true));
    puts("PASS: trace 0 disables probes, no-FMV installs only the four required overrides");
}

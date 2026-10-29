#include "../ps3recomp/runtime/ppu/ppu_tls_image.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
int main() {
    unsigned char memory[4096];
    for (unsigned address : {64u,120u,256u}) {
        memset(memory,0xAC,sizeof memory);
        for(unsigned i=0;i<8;i++)memory[address+i]=(unsigned char)(0x10+i);
        assert(ps3_ppu_tls_copy_image(memory,sizeof memory,1024,1024,address,8,680)==0);
        for(unsigned i=0;i<8;i++)assert(memory[1024+i]==0x10+i);
        for(unsigned i=8;i<1024;i++)assert(memory[1024+i]==0);
        assert(memory[1023]==0xAC&&memory[2048]==0xAC);
    }
    assert(ps3_ppu_tls_copy_image(memory,sizeof memory,1024,1024,64,16,8)==-1);
    assert(ps3_ppu_tls_copy_image(memory,sizeof memory,1024,8,64,8,680)==-1);
    assert(ps3_ppu_tls_copy_image(memory,sizeof memory,4090,32,64,8,8)==-1);
    assert(ps3_ppu_tls_copy_image(memory,sizeof memory,1024,1024,4090,8,8)==-1);
    assert(ps3_ppu_tls_copy_image(memory,sizeof memory,64,1024,64,8,8)==-1);
    assert(ps3_ppu_tls_copy_image(memory,sizeof memory,1024,1024,0,0,0)==0);
    puts("PASS: version-independent TLS templates, zero-fill, capacity/range/overlap checks");
}

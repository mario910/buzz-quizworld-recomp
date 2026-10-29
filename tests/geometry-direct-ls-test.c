#define TEST_DIRECT_GEOMETRY_LS
#define main geometry_random_states
#include "native-geometry-test.c"
#undef main
static spu_context ls_reference;
static uint8_t ls_direct[SPU_LS_SIZE];
int main(void){
    ls_reference.ls=ls_reference.ls_store;ls_reference.image_id=41;
    for(unsigned i=0;i<SPU_LS_SIZE;++i)ls_reference.ls[i]=(uint8_t)word();
    memcpy(ls_direct,ls_reference.ls,SPU_LS_SIZE);
    for(uint32_t address=0;address<SPU_LS_SIZE+32;++address){
        uint32_t wrapped=address|0xf0000000u;
        u128 ref=spu_ls_read128(&ls_reference,wrapped),got=buzz_geometry_ls_read(ls_direct,wrapped);
        assert(!memcmp(&ref,&got,16));
        u128 data;for(unsigned n=0;n<4;++n)data._u32[n]=word();
        spu_ls_write128(&ls_reference,wrapped,data);buzz_geometry_ls_write(ls_direct,wrapped,data);
        unsigned target=wrapped&(SPU_LS_MASK&~15u);
        assert(!memcmp(ls_reference.ls+target,ls_direct+target,16));
    }
    assert(!memcmp(ls_reference.ls,ls_direct,SPU_LS_SIZE));
    puts("PASS: private LS loads/stores at every byte alignment, 256 KiB wrap, BE lanes, final whole-memory equality");
    return geometry_random_states();
}

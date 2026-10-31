#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_index_triangles.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
}
#include "../analysis/native-geometry/phase3/test-index-triangles-reference.inc"
static uint32_t random_word(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void seed(spu_context& c,unsigned trial)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x7a90;uint32_t state=trial+1;
    for(auto& r:c.gpr)for(auto& w:r._u32)w=random_word(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const auto w=random_word(state);memcpy(c.ls+at,&w,4);}
    index_triangles_oracle::seed_tables(c);
    const unsigned counts[]={1,2,3,4,5,16,31,64,128,256,1024,2048,4096};
    c.gpr[22]._u32[0]=counts[trial%13];c.gpr[18]._u32[0]=0xe800+(trial&15);c.gpr[5]._u32[0]=trial&1?0:48;
    c.gpr[20]._u32[0]=0x1a000+((trial/16)&15);c.gpr[31]._u32[0]=0x16002+((trial/256)&15);
    c.gpr[1]._u32[0]=0x39000-432+(trial&15);c.gpr[24]._u32[0]=0xd690;c.gpr[27]._u32[0]=0xd790;
    c.gpr[83]=spu_ls_read128(&c,0xd980);c.gpr[85]=spu_ilh(0x303);
    c.gpr[33]=spu_ls_read128(&c,0xd180);c.gpr[34]=spu_ls_read128(&c,0xd170);c.gpr[35]=spu_ls_read128(&c,0xd160);
    c.gpr[36]=spu_ila(0xf000);c.gpr[37]=spu_il(3840);c.gpr[38]=spu_ila(0xaaaa);
    c.gpr[3]._u32[0]=c.gpr[7]._u32[0]=c.gpr[8]._u32[0]=0;c.gpr[12]._u32[0]=trial%9;c.gpr[21]._u32[0]=(trial%9)*2;
    if(trial&16){for(unsigned reg:{33u,34u,35u,36u,37u,38u,83u,85u})for(auto& w:c.gpr[reg]._u32)w=random_word(state);}
    c.host_depth=trial%3;c.policy_mode=trial%3;c.resident_ovl=trial%5;c.int_enable=trial&1;c.event_mask=trial*13;c.mfc_tag_status=trial*123;c.resv_valid=trial&1;
}
static void copy(spu_context& to,const spu_context& from){memcpy(&to,&from,sizeof to);to.ls=to.ls_store;}
static u128 scalar_quad(const unsigned char* ls,unsigned address)
{
    u128 value{};address&=0x3fff0;
    for(unsigned lane=0;lane<4;++lane)for(unsigned b=0;b<4;++b)value._u32[lane]=(value._u32[lane]<<8)|ls[address+lane*4+b];
    return value;
}
static void scalar_store(unsigned char* ls,unsigned address,u128 value)
{
    address&=0x3fff0;
    for(unsigned lane=0;lane<4;++lane)for(unsigned b=0;b<4;++b)ls[address+lane*4+b]=uint8_t(value._u32[lane]>>(24-b*8));
}
static void zero_model(spu_context& c)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x7a90;
    c.gpr[18]._u32[0]=0xe800;c.gpr[20]._u32[0]=0x1a000;c.gpr[24]._u32[0]=0x10000;c.gpr[27]._u32[0]=0x11000;
    c.gpr[31]._u32[0]=0x12000;c.gpr[22]._u32[0]=2;c.gpr[1]._u32[0]=0x39000-432;
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    GeometryIndexTriangles p;const char* why=nullptr;
    if(!read_geometry_index_triangles(native,p,&why)){fprintf(stderr,"unexpected triangle reject%u %s\n",trial,why);abort();}
    const auto tail=index_triangles_oracle::buzz_job_6eba_spu_func_00002738;
    g_spu_trampoline_fn=nullptr;index_triangles_oracle::buzz_job_6eba_spu_func_00007A90(&reference);assert(g_spu_trampoline_fn==tail);
    g_spu_trampoline_fn=nullptr;stitch_geometry_index_triangles(native,p,tail);const auto d=compare_function_state(native,reference);
    if(!d.exact()){
        fprintf(stderr,"triangle trial%u N%u r%d byte%d LS%X/%u state%s\n",trial,p.iterations,d.first_register,d.first_register_byte,d.first_ls,d.ls_bytes,d.first_state?d.first_state:"equal");
        for(unsigned reg=0;reg<128;++reg)if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)){
            fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }abort();
    }
    assert(g_spu_trampoline_fn==tail && native.host_depth==reference.host_depth && native.image_id==reference.image_id && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();unsigned exact=0;
    for(unsigned trial=0;trial<4096;++trial){seed(*native,trial);copy(*reference,*native);compare(*native,*reference,trial);++exact;}
    for(unsigned shape=0;shape<12;++shape)for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial);native->gpr[22]._u32[0]=4+trial%8;
        switch(shape){
        case 0:native->gpr[20]._u32[0]=native->gpr[18]._u32[0];break;
        case 1:native->gpr[31]._u32[0]=native->gpr[18]._u32[0]+2;break;
        case 2:native->gpr[24]._u32[0]=native->gpr[27]._u32[0]=native->gpr[18]._u32[0];break;
        case 3:native->gpr[1]._u32[0]=native->gpr[18]._u32[0]-432+16;break;
        case 4:native->gpr[31]._u32[0]=0xfffffff2;native->gpr[28]._u32[0]=0;break;
        case 5:native->gpr[20]._u32[0]=SPU_LS_SIZE-16+(trial&15);break;
        case 6:native->gpr[24]._u32[0]=0xfffffff0;native->gpr[27]._u32[0]=SPU_LS_SIZE-16;break;
        case 7:native->gpr[24]._u32[0]=0xe660;native->gpr[27]._u32[0]=0xe670;break;
        case 8:native->gpr[3]._u32[0]=0xfffffff0;native->gpr[7]._u32[0]=0x3ff10;native->gpr[8]._u32[0]=0x40050;break;
        case 9:native->gpr[1]._u32[0]=0xfffffff0;break;
        case 10:native->gpr[18]._u32[0]=0x1e800+(trial&15);native->gpr[5]._u32[0]=0xffff0000;break;
        case 11:native->gpr[24]._u32[0]=0x2d690;native->gpr[27]._u32[0]=0x2d790;break;
        }
        copy(*reference,*native);compare(*native,*reference,10000+shape*64+trial);++exact;
    }
    for(unsigned trial=0;trial<16;++trial){seed(*native,trial);native->gpr[22]._u32[0]=4225;native->gpr[5]._u32[0]=0;copy(*reference,*native);compare(*native,*reference,20000+trial);++exact;}
    seed(*native,0);FILE* fixture=fopen("analysis/native-geometry/phase3/index-audit-v1/pc_7A90.bin","rb");assert(fixture);
    assert(fread(native->gpr,sizeof native->gpr,1,fixture)==1 && fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
    copy(*reference,*native);compare(*native,*reference,30000);++exact;
    auto expected=std::make_unique<unsigned char[]>(SPU_LS_SIZE);
    // Independent frame proof: all source/control bytes are zero, so every
    // output quad is zero. Apply those writes to a byte model, THEN read the
    // return/nonvolatile spills, including output/frame aliases and wrapping.
    for(unsigned shape=0;shape<5;++shape)for(unsigned bias=0;bias<16;++bias){
        zero_model(*native);const unsigned output=0xe800+bias,step=bias&1?0:48,n=2+bias%2;
        const unsigned frame_ends[]={0x3d000u+bias,output+0x100,output-16,output+0x190,8u+bias};const unsigned frame=frame_ends[shape];
        native->gpr[18]._u32[0]=output;native->gpr[5]=spu_il(step);native->gpr[22]._u32[0]=n;
        for(unsigned lane=0;lane<4;++lane){native->gpr[1]._u32[lane]=lane?0xfefe1000u+lane*0x111u+bias:frame-432;
            if(lane)native->gpr[18]._u32[lane]=0xabcd0000u+lane*0x111u+bias;native->gpr[95]._u32[lane]=0x12340000u+lane*31+bias;}
        const u128 stack=native->gpr[1],manager=native->gpr[95],out=native->gpr[18];
        for(unsigned reg=0;reg<26;++reg){u128 spill{};for(unsigned lane=0;lane<4;++lane)spill._u32[lane]=0x90700000u+reg*0x10103u+lane*0x230507u+bias;
            scalar_store(native->ls,reg==24?frame+16:reg==25?frame-0x190:frame-(reg+1)*16,spill);}
        memcpy(expected.get(),native->ls,SPU_LS_SIZE);
        for(unsigned i=0;i<n;++i){const unsigned at=output+(i?step+(i-1)*48:0);memset(expected.get()+(at&0x3fff0),0,48);}
        copy(*reference,*native);compare(*native,*reference,40000+shape*16+bias);++exact;
        assert(!memcmp(native->ls,expected.get(),SPU_LS_SIZE) && !memcmp(&native->gpr[3],&manager,16));
        for(unsigned lane=0;lane<4;++lane){assert(native->gpr[1]._u32[lane]==stack._u32[lane]+432);
            assert(native->gpr[4]._u32[lane]==((out._u32[lane]+step+(n-1)*48+15)&~15u));}
        for(unsigned reg=80;reg<=103;++reg){const u128 spill=scalar_quad(expected.get(),frame-(reg-79)*16);assert(!memcmp(&native->gpr[reg],&spill,16));}
        for(unsigned reg:{0u,126u}){const u128 spill=scalar_quad(expected.get(),reg?frame-0x190:frame+16);assert(!memcmp(&native->gpr[reg],&spill,16));}
    }
    // High-LS LUTs are mutable. Constant-zero pack masks make all stores zero;
    // both the next iteration's prefetch and post-store controls must see it.
    for(unsigned shape=0;shape<2;++shape)for(unsigned bias=0;bias<16;++bias){
        zero_model(*native);const unsigned output=0xe800+bias;native->gpr[18]._u32[0]=output;
        native->gpr[24]._u32[0]=output;native->gpr[27]._u32[0]=output+shape*16;
        for(unsigned reg:{33u,34u,35u})native->gpr[reg]=spu_splat_u32(0x80808080);
        scalar_store(native->ls,output,spu_splat_u32(0xffffffff));scalar_store(native->ls,output+shape*16,spu_splat_u32(0xe0e0e0e0));
        copy(*reference,*native);compare(*native,*reference,50000+shape*16+bias);++exact;
        for(unsigned reg:{9u,14u,19u,29u})for(auto word:native->gpr[reg]._u32)assert(word==0);
        for(unsigned b=0;b<48;++b)assert(native->ls[(output&~15u)+b]==0);
    }
    for(unsigned trial=0;trial<9;++trial){seed(*native,0);switch(trial){
        case 0:native->ls=reference->ls_store;break;case 1:native->gpr[18]._u32[0]=0;break;case 2:native->gpr[18]._u32[0]=0xe67f;break;
        case 3:native->gpr[18]._u32[0]=SPU_LS_SIZE;break;case 4:native->gpr[22]._u32[0]=0;break;
        case 5:native->gpr[22]._u32[0]=0xffffffff;break;case 6:native->gpr[18]._u32[0]=SPU_LS_SIZE-32;break;
        case 7:native->gpr[22]._u32[0]=2;native->gpr[5]._u32[0]=SPU_LS_SIZE;break;
        case 8:native->gpr[22]._u32[0]=2;native->gpr[5]._u32[0]=0xffff0000;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryIndexTriangles p;p.iterations=123;const char* why=nullptr;
        assert(!read_geometry_index_triangles(*native,p,&why) && why && p.iterations==123 && !memcmp(native.get(),reference.get(),sizeof(*native)));
    }
    printf("PASS index triangles: %u strict full-state/LS; 80 independent frame models; 32 mutable-LUT proofs; 9 unchanged rejects\n",exact);
}

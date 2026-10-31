/* AEA0/7390 index control, frames, zero-width setup and both publications. */
#pragma once
#include "native_geometry_commands.h"
#include <cstdlib>
namespace buzz {
enum class IndexControlPath { empty,raw,zero,low,high };
struct GeometryIndexControl { IndexControlPath path=IndexControlPath::empty;unsigned width=0; };
inline bool read_geometry_index_control(const spu_context& c,GeometryIndexControl& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.int_enable && (c.event_status&c.event_mask))return reject("pending interrupt");
    const unsigned manager=c.gpr[3]._u32[0]&~15u,stack=c.gpr[1]._u32[0],frame=stack&~15u;
    if(c.gpr[3]._u32[0]>=SPU_LS_SIZE || manager<0xe680 || uint64_t(manager)+0x190>SPU_LS_SIZE)
        return reject("index manager span");
    if(stack>=SPU_LS_SIZE || frame<0xe680+64 || uint64_t(frame)+32>SPU_LS_SIZE)return reject("outer index frame");
    GeometryIndexControl p;
    const unsigned indices=geometry_job_be16(c.ls+manager+0x180);
    if(!indices){out=p;return true;}
    if(geometry_output_overlap(manager,0x190,frame-64,96))return reject("outer frame/manager alias");
    const unsigned flavor=c.ls[manager+6]>>4;
    if(flavor!=2 && flavor!=3){p.path=IndexControlPath::raw;out=p;return true;}
    const unsigned base=c.gpr[4]._u32[0];
    if(frame<0xe680+496 || geometry_output_overlap(manager,0x190,frame-496,528))return reject("decoder frame/manager alias");
    if((base&15) || base<0xe680 || uint64_t(base)+16>SPU_LS_SIZE ||
       geometry_output_overlap(base,16,frame-496,528))return reject("index header/alignment/frame alias");
    if(indices%3)return reject("non-triangle index count");
    const unsigned triangles=indices/3,deltas=geometry_job_be16(c.ls+base),flags=geometry_job_be16(c.ls+base+4),width=c.ls[base+6];
    if(width>16)return reject("index packed width");
    const unsigned capacity=geometry_job_be32(c.ls+manager+0x17c),commands=(triangles+3)/4,groups=(deltas+31)/32;
    if(capacity<0xe680 || capacity>SPU_LS_SIZE)return reject("index capacity");
    const unsigned scratch=base+((8+flags+commands+15)&~15u);
    auto safe=[&](unsigned at,uint64_t bytes){
        const unsigned physical=at&~15u;
        return at<SPU_LS_SIZE && physical>=0xe680 && uint64_t(physical)+bytes<=SPU_LS_SIZE &&
            !geometry_output_overlap(physical,unsigned(bytes),manager,0x190) &&
            !geometry_output_overlap(physical,unsigned(bytes),frame-496,528);
    };
    if(width){
        if(uint64_t(scratch)+groups*64>capacity)return reject("delta capacity frontier");
        const unsigned packed=(4*width*(groups-1))&65535,n=(packed+(width>8?10:8)*width)/(4*width)+1;
        u128 offset=spu_il(int((groups-1)*64)),increment=spu_ls_read128(const_cast<spu_context*>(&c),0xd120);
        const u128 selector=spu_ls_read128(const_cast<spu_context*>(&c),0xda30);
        for(unsigned i=0;i<n;++i){
            for(unsigned quad=0;quad<4;++quad)if(!safe(scratch+quad*16+offset._u32[0],16))return reject("bit-unpack write span/alias");
            offset=spu_a(offset,increment);increment=spu_shufb(increment,increment,selector);
        }
    }else if(!safe(scratch,16))return reject("zero-width write span/alias");
    if(deltas && !safe(scratch,uint64_t(groups)*64))return reject("delta write span/alias");
    const unsigned expanded=base+((triangles*6+15)&~15u),expansion_groups=(flags+1)/2;
    const unsigned command_output=expanded+expansion_groups*32;
    if(command_output>capacity)return reject("expansion capacity frontier");
    if(!safe(expanded,uint64_t(expansion_groups?expansion_groups:1)*32))return reject("expansion write span/alias");
    if(((uint64_t(command_output)+commands+15)&~15ull)>capacity)return reject("command capacity frontier");
    if(!safe(command_output,uint64_t((commands+15)/16)*16))return reject("command write span/alias");
    if(!safe(base,uint64_t((triangles+7)/8)*48))return reject("triangle write span/alias");
    p.width=width;p.path=width==0?IndexControlPath::zero:width<=8?IndexControlPath::low:IndexControlPath::high;
    out=p;return true;
}
inline buzz_geometry_job_fn run_geometry_index_region(spu_context& c,buzz_geometry_job_fn root,
                                                      buzz_geometry_job_fn tail,buzz_geometry_job_fn other=nullptr)
{
    g_spu_trampoline_fn=nullptr;root(&c);
    for(unsigned steps=0;g_spu_trampoline_fn!=tail && (!other || g_spu_trampoline_fn!=other);++steps){
        if(!g_spu_trampoline_fn || steps>=8)std::abort();
        // A failed invariant must stop before an original reporting handler
        // can perform I/O. Only the known pure data continuations may drain.
        switch(c.pc){case 0x7640:case 0x7c20:case 0x7cd0:case 0x77e8:case 0x7770:case 0x78a8:case 0x79c8:break;
            default:std::fprintf(stderr,"[geometry-job/index-control] unexpected data continuation %X\n",c.pc);std::abort();}
        const auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);
    }
    const auto boundary=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;return boundary;
}
inline void prepare_geometry_index_decoder(spu_context& c)
{
    auto& r=c.gpr;const u128 stack=r[1];
    r[2]=spu_andi(r[4],15);spu_ls_write128(&c,stack._u32[0]-0x190,r[126]);r[5]=spu_ila(0x73a0);r[126]=spu_link(0x73a0);
    spu_ls_write128(&c,stack._u32[0]-0xc0,r[91]);r[91]=r[4];
    spu_ls_write128(&c,stack._u32[0]-0x100,r[95]);r[95]=r[3];spu_ls_write128(&c,stack._u32[0]+16,r[0]);r[126]=spu_sf(r[5],r[126]);
    for(unsigned reg=80;reg<=90;++reg)spu_ls_write128(&c,stack._u32[0]-(reg-79)*16,r[reg]);
    for(unsigned reg=92;reg<=94;++reg)spu_ls_write128(&c,stack._u32[0]-(reg-79)*16,r[reg]);
    for(unsigned reg=96;reg<=103;++reg)spu_ls_write128(&c,stack._u32[0]-(reg-79)*16,r[reg]);
    spu_ls_write128(&c,stack._u32[0]-432,stack);r[1]=spu_ai(stack,-432);
    r[17]=spu_ila(0xaaab);r[18]=spu_ls_read128(&c,r[95]._u32[0]+0x180);r[7]=spu_ila(65535);
    r[84]=spu_ls_read128(&c,r[91]._u32[0]);r[101]=spu_ila(65535);r[83]=spu_ls_read128(&c,0xd980);
    r[15]=spu_ilh(0x8006);r[99]=spu_ls_read128(&c,0xda30);r[13]=spu_ilh(0x606);r[92]=r[91];r[85]=spu_ilh(0x303);
    r[16]=spu_rotqbyi(r[18],14);r[14]=spu_shlqbyi(r[84],2);r[81]=spu_shufb(r[84],r[84],r[15]);r[90]=spu_shufb(r[84],r[84],r[13]);
    r[11]=spu_mpy(r[16],r[17]);r[12]=spu_and(r[14],r[7]);r[82]=spu_and(r[81],r[7]);r[100]=spu_shli(r[12],3);
    r[89]=spu_and(r[101],r[11]);r[9]=spu_rotmi(r[100],-3);r[103]=spu_ai(r[89],7);r[10]=spu_a(r[89],r[103]);
    r[102]=spu_ai(r[9],8);r[87]=spu_rotmi(r[10],-3);r[3]=spu_a(r[102],r[87]);r[8]=spu_ai(r[3],15);
    r[6]=spu_andi(r[8],-16);r[88]=spu_a(r[92],r[6]);
    if(!r[82]._u32[0])return;
    r[96]=spu_rotmi(r[84],-16);r[27]=spu_ls_read128(&c,r[95]._u32[0]+0x170);r[24]=spu_shlhi(r[81],2);r[28]=r[3];
    r[86]=spu_ai(r[88],48);r[98]=spu_a(r[92],r[28]);r[26]=spu_ai(r[96],31);r[94]=spu_ai(r[88],16);
    r[25]=spu_rotmi(r[26],-5);r[20]=spu_rotqbyi(r[27],12);r[93]=spu_ai(r[88],32);r[23]=spu_ai(r[25],-1);
    r[80]=spu_shli(r[23],6);r[21]=spu_mpy(r[24],r[23]);r[22]=spu_ai(r[80],16);r[19]=spu_a(r[22],r[86]);
    r[97]=spu_and(r[21],r[7]);r[7]=spu_cgt(r[19],r[20]);
}
inline void prepare_geometry_index_expansion(spu_context& c)
{
    auto& r=c.gpr;
    r[27]=spu_a(r[89],r[89]);r[40]=spu_ls_read128(&c,r[95]._u32[0]+0x170);r[82]=spu_ai(r[100],15);r[78]=spu_a(r[27],r[89]);
    r[77]=spu_andi(r[82],-16);r[39]=spu_a(r[78],r[78]);r[31]=spu_a(r[77],r[77]);r[36]=spu_ai(r[39],15);
    r[89]=spu_rotqbyi(r[40],12);r[81]=spu_ai(r[92],2);r[80]=spu_andi(r[36],-16);r[76]=spu_a(r[91],r[80]);
    r[100]=spu_a(r[31],r[76]);r[91]=spu_clgt(r[100],r[89]);
}
inline void publish_geometry_index_end(spu_context& c)
{
    auto& r=c.gpr;r[6]=spu_ls_read128(&c,r[3]._u32[0]+0x50);r[5]=spu_cwd(r[1],8);
    r[2]=spu_shufb(r[4],r[6],r[5]);spu_ls_write128(&c,r[3]._u32[0]+0x50,r[2]);SPU_RET(&c);
}
inline void execute_geometry_index_control(spu_context& c,const GeometryIndexControl& p,const buzz_geometry_index_functions& f)
{
    g_spu_trampoline_fn=nullptr; // Original control branches replace the incoming continuation.
    auto& r=c.gpr;r[5]=spu_ls_read128(&c,r[3]._u32[0]+0x180);
    spu_ls_write128(&c,r[1]._u32[0]-16,r[80]);r[80]=r[3];spu_ls_write128(&c,r[1]._u32[0]-32,r[81]);r[81]=r[4];
    spu_ls_write128(&c,r[1]._u32[0]+16,r[0]);spu_ls_write128(&c,r[1]._u32[0]-64,r[1]);r[1]=spu_ai(r[1],-64);r[2]=spu_rotqbyi(r[5],14);
    if(p.path!=IndexControlPath::empty){
        r[12]=spu_ls_read128(&c,r[3]._u32[0]);r[11]=spu_rotqbyi(r[12],3);r[10]=spu_andi(r[11],255);
        r[9]=spu_rothmi(r[10],-4);r[8]=spu_andi(r[9],15);r[7]=spu_ai(r[8],-2);r[6]=spu_clgti(r[7],1);
        if(p.path!=IndexControlPath::raw){
            r[0]=spu_link(0xaf10);const int32_t image=c.image_id;++c.host_depth;
            prepare_geometry_index_decoder(c);
            if(p.width && r[7]._u32[0]){std::fprintf(stderr,"[geometry-job/index-control] unexpected delta capacity\n");std::abort();}
            if(p.width){
                c.pc=0x74f0;run_geometry_index_region(c,f.unpack,f.deltas,f.expansion_setup);
            }else{
                r[96]=spu_rotmi(r[84],-16);r[43]=spu_il(0);r[31]=r[88];spu_ls_write128(&c,r[88]._u32[0],r[43]);
                r[44]=spu_ilh(0x203);r[10]=spu_shufb(r[84],r[84],r[44]);c.pc=r[96]._u32[0]?0x7730:0x77f8;
            }
            if(c.pc==0x7730)run_geometry_index_region(c,f.deltas,f.expansion_setup);
            prepare_geometry_index_expansion(c);if(r[91]._u32[0]){std::fprintf(stderr,"[geometry-job/index-control] unexpected expansion capacity\n");std::abort();}
            c.pc=0x7838;run_geometry_index_region(c,f.expand,f.relocate);
            run_geometry_index_region(c,f.relocate,f.triangles);run_geometry_index_region(c,f.triangles,f.publish);
            publish_geometry_index_end(c);spu_drain_call(&c,0xaf10);--c.host_depth;spu_img_restore(&c,image);
        }
        r[13]=spu_ls_read128(&c,r[80]._u32[0]+0x50);r[3]=spu_cwd(r[1],0);r[4]=spu_shufb(r[81],r[13],r[3]);
        spu_ls_write128(&c,r[80]._u32[0]+0x50,r[4]);
    }
    r[1]=spu_ai(r[1],64);r[0]=spu_ls_read128(&c,r[1]._u32[0]+16);r[80]=spu_ls_read128(&c,r[1]._u32[0]-16);
    r[81]=spu_ls_read128(&c,r[1]._u32[0]-32);SPU_RET(&c);
}
}

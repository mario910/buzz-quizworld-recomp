/* Asset-free differential trace. Compare files from the frozen pre-change
 * engine and the generated adapter; no pointer identities or struct padding. */
#define main sdk_renderer_tests_main
#include "../ps3recomp/libs/video/tests/test_rsx_draw_engine.c"
#undef main
#include <assert.h>
#include "rsx_fp_decompiler.h"
static FILE* trace;
static void record(const void* data,u32 bytes) {
    assert(fwrite(&bytes,4,1,trace)==1);
    if(bytes)assert(fwrite(data,1,bytes,trace)==bytes);
}
static u32 pipeline(void* user,const char* vs,const char* ps,const rsx_be_render_state* rs,
                    const rsx_vertex_layout_plan* layout,u32 stride,rsx_be_format rt,u32 targets) {
    record(vs,(u32)strlen(vs));record(ps,(u32)strlen(ps));
    record(rs,sizeof(*rs)); /* decode_render_state zeroes the whole object */
    record(&layout->mask,4);record(&layout->stride,4);record(&layout->count,1);
    record(layout->attrs,sizeof layout->attrs);record(layout->slot_by_attr,sizeof layout->slot_by_attr);
    record(&stride,4);record(&rt,sizeof rt);record(&targets,4);
    return stub_pipeline_create(user,vs,ps,rs,layout,stride,rt,targets);
}
static void constants(void* user,const void* data,u32 bytes) {
    record(data,bytes);stub_bind_ps(user,data,bytes);
}
static void draw(void* user,rsx_topology type,const void* vertices,u32 count,u32 stride,
                 const u32* indices,u32 index_count) {
    record(&type,sizeof type);record(&count,4);record(&stride,4);
    record(vertices,count*stride);record(indices,index_count*4);
    stub_draw(user,type,vertices,count,stride,indices,index_count);
}
int main(int argc,char** argv) {
    assert(argc==2);trace=fopen(argv[1],"wb");assert(trace);
    vm_base=(u8*)calloc(1,GUEST_BYTES);assert(vm_base);ppu_vm_size=GUEST_BYTES;
    const u32 ops[]={0x17,0x18,0x31,0x2f,0x19,0x33,0x34};
    for(u32 fixture=0;fixture<224;++fixture) {
        engine_up();rsx_draw_backend backend=g_stub_backend;
        backend.pipeline_create=pipeline;backend.bind_ps_constants=constants;backend.draw=draw;
        rsx_draw_engine_set_backend(&backend);
        const u32 unit=(fixture/7)%16,method=M_TEX_UNIT+unit*32;
        m(method,TEX_OFFSET);m(method+4,2u|(2u<<4)|(0xa5u<<8)|(1u<<16));
        m(method+8,((fixture&15u)<<20)|((fixture&1u)<<12));
        m(method+12,0x80000000u);m(method+16,fixture&1?0xAAE4u:0xAA1Bu);
        m(method+20,(fixture&15u)<<28);m(method+24,(4u<<16)|4u);
        u8* fp=vm_base+0x30000;memset(fp,0,128);
        const u32 first=(ops[fixture%7]<<24)|(unit<<17)|(0xfu<<9)|((fixture&1u)<<21);
        program_word(fp,first);program_word(fp+4,2u|(1u<<11)|(2u<<13)|(3u<<15)|(7u<<18));
        program_word(fp+32,(1u<<24)|(0xfu<<9)|1u);program_word(fp+36,2u);
        program_vertex_input(0);m(0x08e4,0x30000|2u);m(0x1d60,0x40);
        g_rsx_fp_nonfinite_constants=0;
        for(u32 phase=0;phase<16;++phase) {
            program_word(fp+16,phase&1?0x7f800000u:0x3f000000u);
            program_float(fp+48,0.25f*(phase+1));
            if(phase==2)m(method+8,((15u-(fixture&15u))<<20)|((!(fixture&1u))<<12));
            if(phase==3)program_word(fp,first^(1u<<21));
            if(phase==4)program_vertex_input(3);
            if(phase==5)program_word(fp,first|1u);
            if(phase==6)m(0x08e4,0);
            if(phase==7){m(0x08e4,0x30000|2u);program_word(fp,first);}
            /* Re-decode every draw: cube validity, unnormalized dimensions,
             * volume capability, enable changes and relocation must not reuse
             * a previous draw's register snapshot. */
            if(phase==8)m(method+4,2u|4u|(2u<<4)|(0xe5u<<8)|(1u<<16));
            if(phase==9)m(method+24,(4u<<16)|8u);
            if(phase==10){m(method+4,2u|(3u<<4)|(0xa5u<<8)|(1u<<16));m(0x1840+unit*4,(4u<<20)|16u);}
            if(phase==11)m(method+12,0);
            if(phase==12){m(method+12,0x80000000u);m(method+24,4u);}
            if(phase==13){m(method+4,2u|(2u<<4)|(0xa5u<<8)|(1u<<16));m(method+24,(4u<<16)|4u);}
            if(phase==14){m(method+20,0x2020u);m(method+28,0x12345678u);}
            if(phase==15)m(method,TEX_OFFSET+64u);
            record(&fixture,4);record(&phase,4);draw_triangle();rsx_draw_engine_present();
            record(&stub.n_draw,sizeof stub.n_draw);record(&stub.n_pipeline_create,sizeof stub.n_pipeline_create);
            record(&stub.bound_mask,4);record(stub.bound_tex,sizeof stub.bound_tex);
            record(&g_rsx_fp_nonfinite_constants,sizeof g_rsx_fp_nonfinite_constants);
        }
        engine_down();
    }
    /* Twelve distinct vertices exercise bulk conversion and the array fast
     * path. Repeat unchanged data, then verify a source-byte mutation. */
    for(u32 type=1;type<=7;++type)for(u32 size=1;size<=4;++size) {
        engine_up();rsx_draw_backend backend=g_stub_backend;
        backend.pipeline_create=pipeline;backend.bind_ps_constants=constants;backend.draw=draw;
        rsx_draw_engine_set_backend(&backend);
        m(M_VTXFMT,type|(size<<4)|(16u<<8));
        for(u32 i=0;i<12*16;++i)vm_base[VTX_OFFSET+i]=(u8)(i*37+type*11+size);
        record(&type,4);record(&size,4);
        for(u32 repeat=0;repeat<4;++repeat) {
            if(repeat==3)vm_base[VTX_OFFSET+33]^=1;
            m(M_BEGIN_END,RSX_PRIMITIVE_TRIANGLES);m(M_DRAW_ARRAYS,11u<<24);m(M_BEGIN_END,0);
            assert(stub.n_draw==(int)repeat+1);rsx_draw_engine_present();
        }
        engine_down();
    }
    /* Fused index expansion must preserve ordering/topology and leave restart,
     * mixed arrays, multiple batches, u32 and unsupported primitives alone. */
    const u32 topologies[]={RSX_PRIMITIVE_TRIANGLES,RSX_PRIMITIVE_TRIANGLE_STRIP,
        RSX_PRIMITIVE_TRIANGLE_FAN,RSX_PRIMITIVE_QUADS,RSX_PRIMITIVE_POINTS,RSX_PRIMITIVE_LINE_STRIP};
    for(u32 fixture=0;fixture<96;fixture++) {
        engine_up();rsx_draw_backend backend=g_stub_backend;
        backend.pipeline_create=pipeline;backend.bind_ps_constants=constants;backend.draw=draw;
        rsx_draw_engine_set_backend(&backend);
        const u32 mode=fixture%8,is32=mode==1,local=mode==7;
        m(0x181c,0x60000);m(0x1820,(is32?0:0x10)|(local?0:1));
        m(0x1dac,mode==2);m(0x1db0,0xffff);
        for(u32 v=0;v<32;v++)for(u32 c=0;c<4;c++)program_float(vm_base+VTX_OFFSET+v*16+c*4,(float)(v+c)/32);
        u8* input=vm_base+(local?GUEST_LOCAL_EA:0)+0x60000;
        for(u32 phase=0;phase<8;phase++) {
            /* Include incomplete triangle tails in the direct-index path. */
            const u32 n=mode==3?12:10+phase%4;
            for(u32 i=0;i<n;i++) {
                const u32 index=mode==2 && i==6?0xffff:mode==6?0xffff:((i*3+phase/4)%11);
                if(is32)program_word(input+4*i,index);else {input[2*i]=(u8)(index>>8);input[2*i+1]=(u8)index;}
            }
            if(mode==5)m(0x181c,GUEST_BYTES-8); /* complete mapping fails */
            record(&fixture,4);record(&phase,4);
            m(M_BEGIN_END,topologies[(fixture/8)%6]);
            if(mode==4)m(M_DRAW_ARRAYS,2u<<24);
            if(mode==3){m(0x1824,5u<<24);m(0x1824,(5u<<24)|6u);}
            else m(0x1824,(n-1)<<24);
            m(M_BEGIN_END,0);rsx_draw_engine_present();record(&stub.n_draw,sizeof stub.n_draw);
        }
        engine_down();
    }
    /* Multiple noncontiguous index packets, including an invalid later span.
     * Preserve packet order, overlap, cuts, incomplete tails and fallbacks. */
    for(u32 fixture=0;fixture<48;++fixture) {
        engine_up();rsx_draw_backend backend=g_stub_backend;
        backend.pipeline_create=pipeline;backend.bind_ps_constants=constants;backend.draw=draw;
        rsx_draw_engine_set_backend(&backend);
        const u32 mode=fixture%6,is32=mode==1;
        m(0x181c,0x60000);m(0x1820,is32?1:0x11);
        m(0x1dac,mode==2);m(0x1db0,0xffff);
        for(u32 v=0;v<32;++v)for(u32 c=0;c<4;++c)
            program_float(vm_base+VTX_OFFSET+v*16+c*4,(float)(v+c)/32);
        for(u32 phase=0;phase<4;++phase) {
            const u32 batches=phase==3?256:2+phase;
            for(u32 i=0;i<128;++i) {
                const u32 index=(mode==2 && i%13==0)?0xffff:(i*7+phase)%32;
                if(is32)program_word(vm_base+0x60000+4*i,index);
                else {vm_base[0x60000+2*i]=(u8)(index>>8);vm_base[0x60001+2*i]=(u8)index;}
            }
            record(&fixture,4);record(&phase,4);
            m(M_BEGIN_END,topologies[(fixture/6)%6]);
            if(mode==5)m(M_DRAW_ARRAYS,2u<<24);
            for(u32 b=0;b<batches;++b) {
                u32 first=(63-b*13+phase)&63u;
                if(mode==3 && b==1)first=0xfffff0;
                if(mode==4 && b==1)m(0x181c,0xfffffff0u);
                m(0x1824,((4u+b%4)<<24)|first);
            }
            m(M_BEGIN_END,0);rsx_draw_engine_present();record(&stub.n_draw,sizeof stub.n_draw);
            m(0x181c,0x60000);
        }
        engine_down();
    }
    /* Actual three-input programs exercise the four-vertex converter in the
     * generated engine, not just its leaf helpers. Include every attribute
     * order, indexed/non-indexed sources, all packet tails and byte mutations. */
    const u32 orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    const u32 attrs[3]={0,2,7},types[3]={2,6,3},sizes[3]={3,1,2};
    for(u32 order=0;order<6;++order) {
        engine_up();rsx_draw_backend backend=g_stub_backend;
        backend.pipeline_create=pipeline;backend.bind_ps_constants=constants;backend.draw=draw;
        rsx_draw_engine_set_backend(&backend);
        const u32 source=2u|(1u<<12)|(2u<<10)|(3u<<8);
        m(0x1e9c,0);
        for(u32 slot=0;slot<3;++slot) {
            const u32 attr=attrs[slot],format=orders[order][slot];
            m(0x0b80,(1u<<30)|(63u<<15));
            m(0x0b84,(1u<<22)|(attr<<8)|((source>>9)&0xff));
            m(0x0b88,(source&0x1ff)<<23);m(0x0b8c,(slot==2)|(0xfu<<13));
            m(M_VTXFMT+attr*4,types[format]|(sizes[format]<<4)|(20u<<8));
            m(M_VTXBUF_OFFSET+attr*4,0x80000000u| (VTX_OFFSET+slot*2048));
            for(u32 i=0;i<1024;++i)vm_base[VTX_OFFSET+slot*2048+i]=(u8)(i*13+slot*37+order);
        }
        u8* fp=vm_base+0x30000;memset(fp,0,32);
        program_word(fp,(1u<<24)|(0xfu<<9)|1u);program_word(fp+4,2u);
        program_float(fp+16,0.5f);m(0x08e4,0x30000|2u);m(0x1d60,0x40);
        m(0x181c,0x60000);m(0x1820,0x11);
        for(u32 count=4;count<=35;++count) {
            vm_base[VTX_OFFSET+2048+33]^=1;
            record(&order,4);record(&count,4);
            m(M_BEGIN_END,RSX_PRIMITIVE_TRIANGLES);
            if(count&1) {
                for(u32 i=0;i<count;++i){vm_base[0x60000+i*2]=0;vm_base[0x60001+i*2]=(u8)((i*7)%(count-1));}
                m(0x1824,(count-1)<<24);
            } else m(M_DRAW_ARRAYS,(count-1)<<24);
            m(M_BEGIN_END,0);rsx_draw_engine_present();
            assert(stub.draw_stride==48 && stub.pipeline_input_mask==0x85);
            assert(stub.n_draw==(int)count-3);
        }
        engine_down();
    }
    assert(fclose(trace)==0);free(vm_base);
    puts("PASS trace: program/texture, vertex-format, index and three-input stream fixtures / 4848 draw attempts");
    return 0;
}

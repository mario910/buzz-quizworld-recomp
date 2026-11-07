/* Direct host packet construction. Admission is the existing complete command
 * plan; real preparation/channel I/O and caller ABI boundaries are retained.
 * Volatile SPU temporaries are not part of the host packet representation. */
#pragma once
#include "native_geometry_full_commands.h"
#include <cstdio>
#include <cstdlib>

namespace buzz {
inline bool geometry_host_commands_enabled()
{
#if defined(BUZZ_GEOMETRY_HOST_COMMANDS) && BUZZ_GEOMETRY_HOST_COMMANDS
    static const bool enabled=[]{
        const char* value=std::getenv("BUZZ_HOST_COMMANDS");
        if(value && ((value[0]!='0' && value[0]!='1') || value[1])) {
            std::fprintf(stderr,"BUZZ_HOST_COMMANDS must be 0 or 1\n");std::abort();
        }
        const bool on=!value||value[0]=='1';
        std::fprintf(stderr,"[geometry-host-commands] enabled=%d (strict stage diagnostics retain SPU ABI path)\n",int(on));
        return on;
    }();
    return enabled;
#else
    return false;
#endif
}

struct GeometryHostPackets {
    spu_context& ctx;
    uint32_t cursor;
    void word(uint32_t value) {geometry_job_put32(ctx.ls+cursor,value);cursor+=4;}
    void pair(uint32_t header,uint32_t value) {word(header);word(value);}
    void bind(const GeometryCommandFormat& format,uint32_t mask,uint32_t source) {
        const uint32_t base=geometry_job_be32(ctx.ls+source+4);
        const uint32_t location=geometry_job_be32(ctx.ls+source+8)<<31;
        for(unsigned i=0;i<format.count;++i) {
            const auto& f=format.attributes[i];
            if(!geometry_command_attribute_enabled(mask,f.slot))continue;
            pair(0x41740+f.slot*4,(format.stride<<8)|(unsigned(f.components)<<4)|f.type);
            pair(0x41680+f.slot*4,(base+f.offset)|location);
        }
    }
    void reset(const GeometryCommandFormat& format,uint32_t mask) {
        for(unsigned i=0;i<format.count;++i)
            if(geometry_command_attribute_enabled(mask,format.attributes[i].slot))
                pair(0x41740+format.attributes[i].slot*4,2);
    }
    void triangles(uint32_t count,uint32_t leading,uint32_t offset,uint32_t format) {
        pair(0x41714,0);word(0x8181c);word(offset);word(format);pair(0x41808,5);
        uint32_t first=0,left=count;
        if(leading){pair(0x41824,(leading-1)<<24);first=leading;left-=leading;}
        while(left>0x7ff00) {
            word(0x5ffc1824);
            for(unsigned i=0;i<2047;++i)word(0xff000000u|(first+i*256));
            first+=0x7ff00;left-=0x7ff00;
        }
        word(0x40001824u|(((left+255)/256)<<18));
        while(left>256){word(0xff000000u|first);first+=256;left-=256;}
        if(left)word(((left-1)<<24)|first);
        pair(0x41808,0);
    }
};

// The scalar traversal has the same packet splitting and physical-tail moves
// as the lifted publisher. Only the small vector state needed for live r3 is
// retained. It issues the real ordered MachStat/MFC operations, not completion
// substitutes, and does not bypass validation, coherency, tags or wakeups.
inline void publish_geometry_host_commands(spu_context& ctx,const GeometryCommandPublication& p)
{
    if(!p.destination)return;
    const u128 initial_record=geometry_output_quad(ctx,p.record);
    uint32_t cursor=p.cursor;
    for(unsigned i=0;i<p.count;++i) {
        const auto* record=ctx.ls+p.records+i*32;
        const unsigned semaphore=geometry_job_be32(record+16);
        if(semaphore) {
            geometry_job_put32(ctx.ls+cursor,0x41d6c);
            geometry_job_put32(ctx.ls+cursor+4,semaphore&4080);
            geometry_job_put32(ctx.ls+cursor+8,0x41d74);
            geometry_job_put32(ctx.ls+cursor+12,geometry_job_be32(record+12));
            cursor+=16;geometry_job_put32(ctx.ls+p.record+8,cursor);
        }
    }
    uint32_t end=cursor,next=p.start,previous=0,header=0,payload=0,start_block=0,end_block=0;
    u128 next_vector=spu_rotqby(geometry_output_quad(ctx,p.record),ctx.gpr[4]),previous_vector=spu_il(0);
    u128 start_vector=spu_il(0),end_vector=spu_il(0);
    ctx.gpr[3]=initial_record;
    if(end!=p.start) for(;;) {
        const bool more=end>previous,same_block=start_block==end_block;
        const auto quad=geometry_output_quad(ctx,next&(SPU_LS_SIZE-16));
        const unsigned word=geometry_job_be32(ctx.ls+(next&(SPU_LS_SIZE-4)));
        ctx.gpr[3]=spu_ceq(start_vector,end_vector);
        start_vector=spu_andi(next_vector,-128);start_block=next&~127u;
        if(!more)break;
        const uint32_t old_header=header,old_address=previous;
        const auto old_previous=previous_vector;
        const auto header_vector=spu_rotqby(quad,next_vector);
        previous_vector=next_vector;header=word;previous=next;
        const uint32_t boundary=(old_address+128)&~127u,gap=boundary-old_address-4;
        const uint32_t remainder=payload-gap;
        payload=(word>>16)&8188;next+=payload+4;end_block=(next-4)&~127u;
        const auto payload_vector=spu_and(spu_rotqmbyi(header_vector,-2),spu_il(8188));
        const auto packet_end=spu_a(next_vector,payload_vector);
        end_vector=spu_andi(packet_end,-128);next_vector=spu_ai(packet_end,4);
        if(!same_block) {
            const uint32_t move_end=(end&~127u)+128;
            geometry_job_put32(ctx.ls+old_address,gap?((old_header&0xe003ffffu)|(gap<<16)):0);
            memmove(ctx.ls+boundary,ctx.ls+boundary-4,move_end-boundary);
            const uint32_t relocated=((old_header&0xe003ffffu)|(remainder<<16))+
                ((old_header&0x40000000u)?0:gap);
            geometry_job_put32(ctx.ls+boundary,relocated);
            next=previous=boundary;end+=4;end_block=start_block;
            next_vector=previous_vector=spu_andi(spu_ai(old_previous,128),-128);
            end_vector=start_vector;
        }
    }
    memset(ctx.ls+end,0,p.limit-end);geometry_job_put32(ctx.ls+p.record+8,p.limit);
    (void)buzz_geometry_job_rdch(&ctx,SPU_RdMachStat);ctx.int_enable=0;
    buzz_geometry_job_wrch(&ctx,MFC_LSA,spu_ila(p.start));
    buzz_geometry_job_wrch(&ctx,MFC_EAL,spu_splat_u32(p.destination));
    buzz_geometry_job_wrch(&ctx,MFC_Size,spu_il(p.bytes));
    buzz_geometry_job_wrch(&ctx,MFC_TagID,geometry_output_quad(ctx,p.context+0x150));
    buzz_geometry_job_wrch(&ctx,MFC_Cmd,spu_il(34));
}

inline void geometry_host_publish_call(spu_context& ctx,GeometryCommandAdmissionScope& admission,uint32_t link)
{
    ctx.gpr[0]=spu_link(link);
    GeometryCommandPublication publication;const char* reason=nullptr;
    const auto* forecast=admission.take_publish(ctx);
    if(!read_geometry_command_publication(ctx,publication,&reason,forecast))
        buzz_geometry_job_native_fail(&ctx,reason?reason:"host command publication invariant");
    const int32_t image=ctx.image_id;++ctx.host_depth;
    publish_geometry_host_commands(ctx,publication);ctx.pc=link;
    spu_drain_call(&ctx,link);--ctx.host_depth;spu_img_restore(&ctx,image);
}

inline void emit_geometry_full_commands_host(spu_context& ctx,const GeometryFullCommands& p,
                                            const buzz_geometry_command_functions& functions)
{
    GeometryCommandAdmissionScope admission(ctx,p.reservation,p.raw_end,p.relocated_end,0x1380,0x1a68);
    auto& r=ctx.gpr;const u128 old_stack=r[1],index_record=r[7],facing=r[10];
    const uint32_t offset=index_record._u32[1]&0x1fffffff;
    const uint32_t index_format=spu_ori(spu_andi(spu_shlqbyi(index_record,8),255),16)._u32[0];
    const auto flip=spu_fsm(spu_ceqhi(spu_xsbh(spu_ceqbi(facing,0)),0));
    const uint32_t clockwise=spu_selb(spu_il(1029),spu_il(1028),flip)._u32[0];
    const uint32_t counterclockwise=spu_selb(spu_il(1028),spu_il(1029),flip)._u32[0];
    spu_ls_write128(&ctx,p.stack+16,r[0]);
    for(unsigned reg=80;reg<=89;++reg)spu_ls_write128(&ctx,p.stack-(reg-79)*16,r[reg]);
    spu_ls_write128(&ctx,p.stack-0xb0,r[126]);spu_ls_write128(&ctx,p.stack-0xf0,old_stack);
    r[81]=r[11];r[82]=r[10];r[84]=r[3];r[85]=r[4];r[86]=r[5];r[5]=r[85];
    r[88]=r[9];r[87]=r[8];r[89]=r[12];r[82]=spu_ceqbi(r[82],0);
    r[1]=spu_ai(old_stack,-240);r[83]=spu_shlqbyi(index_record,4);r[82]=spu_xsbh(r[82]);r[80]=spu_shlqbyi(index_record,8);
    r[4]=spu_ai(r[1],32);spu_ls_write128(&ctx,r[1]._u32[0]+0x30,r[6]);
    r[6]=r[86];r[7]=spu_il(1);geometry_command_call(ctx,functions.prepare,0x1380);
    const uint32_t record=p.reservation.output,mask=r[89]._u32[0]&65535;
    GeometryHostPackets packets{ctx,geometry_job_be32(ctx.ls+record+8)};
    packets.bind(p.format,mask,r[1]._u32[0]+48);
    for(unsigned pass=0;pass<p.pass_count;++pass) {
        uint32_t state[16]{};
        if(p.metadata) {
            for(unsigned i=0;i<16;++i)state[i]=geometry_job_be32(ctx.ls+p.metadata+16+pass*64+i*4);
            if(p.groups&2) {
                if(state[0]!=8)packets.pair(0x40304,state[1]);
                if(state[2]!=8){packets.word(0x80308);packets.word(state[3]&0xffffff);packets.word(state[3]>>24);}
            }
            if(p.groups&1) {
                if(state[12]!=8)packets.pair(0x40a74,state[13]);
                if(state[14]!=8)packets.pair(0x40a70,state[15]);
            }
            if(p.groups&4) {
                if(state[6]!=8)packets.pair(0x40310,state[7]);
                if(state[8]!=8){packets.word(0x80314);packets.word((state[9]&65535)*0x10001);packets.word((state[11]&65535)*0x10001);}
            }
            if((p.groups&8) && state[4]!=8) {
                packets.pair(0x4183c,state[5]!=0);
                if(state[5])packets.pair(0x41830,state[5]==2305?clockwise:counterclockwise);
            }
        }
        packets.triangles(p.indices,p.leading,offset,index_format);
    }
    packets.reset(p.format,mask);geometry_job_put32(ctx.ls+record+8,packets.cursor);
    r[4]=spu_ai(r[1],32);r[3]=r[84];r[5]=r[85];r[6]=r[86];r[7]=spu_il(1);
    geometry_host_publish_call(ctx,admission,0x1a68);
    r[0]=spu_ls_read128(&ctx,p.stack+16);r[1]=spu_ai(r[1],240);
    for(unsigned reg=80;reg<=89;++reg)r[reg]=spu_ls_read128(&ctx,p.stack-(reg-79)*16);
    r[126]=spu_ls_read128(&ctx,p.stack-0xb0);
}

inline void emit_geometry_position_commands_host(spu_context& ctx,const GeometryPositionCommands& p,
                                                const buzz_geometry_command_functions& functions)
{
    GeometryCommandAdmissionScope admission(ctx,p.reservation,p.raw_end,p.relocated_end,0x368,0x760);
    auto& r=ctx.gpr;const u128 old_stack=r[1],index_record=r[7],facing=r[10];
    const uint32_t offset=index_record._u32[1]&0x1fffffff;
    const uint32_t index_format=spu_ori(spu_andi(spu_shlqbyi(index_record,8),255),16)._u32[0];
    const auto flip=spu_fsm(spu_ceqhi(spu_xsbh(spu_ceqbi(facing,0)),0));
    const uint32_t winding=spu_selb(spu_il(p.raster==2305?1029:1028),
                                 spu_il(p.raster==2305?1028:1029),flip)._u32[0];
    for(unsigned reg=80;reg<=88;++reg)spu_ls_write128(&ctx,p.stack-(reg-79)*16,r[reg]);
    spu_ls_write128(&ctx,p.stack+16,r[0]);spu_ls_write128(&ctx,p.stack-0xa0,r[126]);
    spu_ls_write128(&ctx,p.stack-0xe0,old_stack);
    r[80]=r[8];r[81]=r[9];r[82]=r[3];r[83]=r[10];r[86]=r[4];r[87]=r[5];r[88]=r[12];
    r[5]=r[4];r[1]=spu_ai(old_stack,-224);r[84]=spu_shlqbyi(index_record,8);r[85]=spu_shlqbyi(index_record,4);
    r[4]=spu_ai(r[1],32);spu_ls_write128(&ctx,r[1]._u32[0]+0x30,r[6]);
    r[6]=r[87];r[7]=spu_il(1);geometry_command_call(ctx,functions.prepare,0x368);
    const uint32_t record=p.reservation.output,mask=r[88]._u32[0]&65535;
    GeometryHostPackets packets{ctx,geometry_job_be32(ctx.ls+record+8)};
    packets.bind(p.format,mask,r[1]._u32[0]+48);
    if(p.metadata && p.kind!=8) {
        packets.pair(0x4183c,p.raster!=0);
        if(p.raster)packets.pair(0x41830,winding);
    }
    packets.triangles(p.indices,p.leading,offset,index_format);
    packets.reset(p.format,mask);geometry_job_put32(ctx.ls+record+8,packets.cursor);
    r[4]=spu_ai(r[1],32);r[3]=r[82];r[5]=r[86];r[6]=r[87];r[7]=spu_il(1);
    geometry_host_publish_call(ctx,admission,0x760);
    r[0]=spu_ls_read128(&ctx,p.stack+16);r[1]=spu_ai(r[1],224);
    for(unsigned reg=80;reg<=88;++reg)r[reg]=spu_ls_read128(&ctx,p.stack-(reg-79)*16);
    r[126]=spu_ls_read128(&ctx,p.stack-0xa0);
}
} // namespace buzz

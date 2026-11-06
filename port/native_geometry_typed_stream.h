/* Shared checked layout for typed descriptor streams and BE halfword gather. */
#pragma once
#include "native_geometry_output.h"
namespace buzz {
struct GeometryTypedColumn { uint32_t offset=0,groups=0; bool crosses=false; };
struct GeometryTypedStream {
    uint32_t columns=0,input_step=0,output_step=0,limit=0;
    GeometryTypedColumn column[16]{}; // bounded by admission; no per-call allocation
};
inline bool read_geometry_typed_stream(const spu_context& ctx,GeometryTypedStream& output,unsigned table_address,const uint32_t* table,bool width_controls_branch,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    GeometryTypedStream p;
    if(!ctx.gpr[121]._u32[0]){output=p;return true;}
    p.columns=ctx.gpr[107]._u32[0];p.input_step=ctx.gpr[83]._u32[0];
    p.output_step=ctx.gpr[81]._u32[0];p.limit=ctx.gpr[80]._u32[0];
    const uint32_t bytes=width_controls_branch?ctx.gpr[25]._u32[0]:4,stride=ctx.gpr[106]._u32[0],attribute=ctx.gpr[42]._u32[0],stack=ctx.gpr[1]._u32[0];
    if(!bytes || bytes>16 || !p.columns || p.columns>16 || !stride || stride>255 || attribute>255 ||
        !p.input_step || (p.input_step&15) || !p.output_step || (p.output_step&15) ||
        p.limit>=SPU_LS_SIZE || !geometry_output_range(stack,0x80))return reject("shape/step/stack");
    for(unsigned byte=0;byte<16;++byte)
        if(ctx.gpr[112]._u8[byte^3]!=byte || ctx.gpr[108]._u8[byte]!=3)return reject("gather registers");
    const auto actual=geometry_output_quad(ctx,table_address);
    if(memcmp(actual._u32,table,16))return reject("typed gather table");
    uint64_t input_begin=SPU_LS_SIZE,input_end=0,output_begin=SPU_LS_SIZE,output_end=0;
    const unsigned bases[]={118,117,116,124};
    for(unsigned row=0;row<4;++row){
        const uint32_t base=ctx.gpr[bases[row]]._u32[0],scratch=ctx.gpr[82]._u32[row];
        if((base&15) || geometry_job_be32(ctx.ls+stack+0x70-row*16)!=uint64_t(base)+16 ||
            ctx.gpr[83]._u32[row]!=p.input_step || ctx.gpr[81]._u32[row]!=p.output_step ||
            ctx.gpr[80]._u32[row]!=p.limit || !geometry_output_range(scratch,16))return reject("input pointers/steps/scratch");
        output_begin=std::min(output_begin,uint64_t(scratch));output_end=std::max(output_end,uint64_t(scratch)+16);
        if(scratch!=stack+0x20 && scratch<stack+0x80 && stack<scratch+16)return reject("scratch/stack metadata alias");
    }
    for(unsigned column=0;column<p.columns;++column){
        GeometryTypedColumn c;c.offset=column*stride+attribute;c.crosses=(c.offset&15)+bytes>16;
        const uint64_t first=uint64_t(ctx.gpr[41]._u32[0])+column*16;
        if(first>INT32_MAX)return reject("preferred cursor");
        c.groups=first>p.limit?1:(p.limit-uint32_t(first))/p.output_step+2;
        if(c.groups>4096)return reject("group count");
        for(unsigned row=0;row<4;++row){
            const uint64_t begin=uint64_t(ctx.gpr[bases[row]]._u32[0])+c.offset;
            const uint64_t end=begin+uint64_t(c.groups)*p.input_step+32;
            if(begin<0xe680 || end>SPU_LS_SIZE)return reject("source lookahead bounds");
            input_begin=std::min(input_begin,begin&~uint64_t{15});input_end=std::max(input_end,end);
            const uint64_t destination=uint64_t(ctx.gpr[41]._u32[row])+column*16;
            const uint64_t last=destination+uint64_t(c.groups-1)*p.output_step;
            if(last>INT32_MAX)return reject("output cursor bounds");
            if(destination<=p.limit){
                const uint64_t physical=destination&~uint64_t{15},last_end=(std::min(last,uint64_t(p.limit))&~uint64_t{15})+16;
                output_begin=std::min(output_begin,physical);output_end=std::max(output_end,last_end);
                if(physical<stack+0x80 && stack<last_end)return reject("destination/stack metadata alias");
            }
        }
        p.column[column]=c;
    }
    if(output_begin<0xe680 || output_end>SPU_LS_SIZE ||
        (input_begin<output_end && output_begin<input_end))return reject("output/source bounds or alias");
    output=p;return true;
}
inline u128 geometry_typed_halfwords(const uint8_t* memory,unsigned source,unsigned offset,bool crosses)
{
    u128 value;
    for(unsigned lane=0;lane<4;++lane){
        const unsigned a=crosses?source+offset+lane*2:source+(offset&~15u)+((offset+lane*2)&15);
        const unsigned b=crosses?a+1:source+(offset&~15u)+((offset+lane*2+1)&15);
        value._u32[lane]=(uint32_t(memory[a])<<24)|(uint32_t(memory[b])<<16);
    }
    return value;
}

} // namespace buzz

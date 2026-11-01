/* Closed-form admission for the output encoders' group walk. The exact walk
 * steps every group of every column to count groups and to keep each scatter
 * quad outside the protected low LS. This proof derives the same counts and
 * checks a superset of the same quads from the walk's arithmetic progressions.
 * It only ever accepts what the walk would accept, with identical counts;
 * when it cannot prove that, the caller runs the unchanged exact walk. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"
#include <cstdint>

namespace buzz {
struct GeometryOutputWalk {
    uint32_t cursor=0,limit=0,step=0,first=0,second=0; // lane-0 control values
    const u128* r=nullptr;                                // route r53..r55, steps r31, after-route r30
};
// Scatter quad check of the walk: (base+route)&0x3fff0 must reach 0xe680.
// 0xe680 is quad aligned, so this is (base+route) mod 2^18 >= 0xe680.
BUZZ_GEOMETRY_HOT bool geometry_output_quad_safe(uint32_t address)
{
    return (address&0x3ffffu)>=0xe680u;
}
// Every quad base+route0+j*step (mod 2^32), j=0..last. Monotonic in exact
// integers, and 2^18 divides 2^32: inside one 2^18 window the lowest residue
// is at an endpoint. A crossing window is left to the exact walk.
BUZZ_GEOMETRY_HOT bool geometry_output_progression_safe(uint32_t base,uint32_t route0,uint32_t step,uint64_t last)
{
    const int64_t start=int64_t(uint32_t(base+route0)),end=start+int64_t(last)*int32_t(step);
    const auto window=[](int64_t x){return x>>18;}; // arithmetic floor division
    if(window(start)!=window(end))return false;
    return ((start<end?start:end)&0x3ffff)>=0xe680;
}
// One column: lane 0 leaves after the first group whose signed cursor exceeds
// the signed limit. Each lane checks route_j (before its own exit) or r30
// (after it), so proving all route_0..route_last and r30 covers every quad.
inline bool prove_geometry_output_column(const GeometryOutputWalk& w,unsigned column,bool cross,unsigned& groups)
{
    const int64_t cursor=int32_t(w.cursor+column*16u),limit=int32_t(w.limit),step=int32_t(w.step);
    uint64_t last=0;
    if(cursor<=limit){
        if(step<=0)return false;
        last=uint64_t((limit-cursor)/step)+1;
        if(cursor+int64_t(last)*step>INT32_MAX)return false; // signed cursor wrap
    }
    if(last>=65536)return false; // the walk's group budget decides
    const u128 route=spu_mpya(w.r[55],spu_a(spu_il(int(column)),w.r[54]),w.r[53]);
    for(unsigned lane=0;lane<4;++lane){
        const uint32_t after=w.r[30]._u32[lane],start=route._u32[lane],stride=w.r[31]._u32[lane];
        if(!geometry_output_quad_safe(w.first+after) ||
           !geometry_output_progression_safe(w.first,start,stride,last))return false;
        if(cross && (!geometry_output_quad_safe(w.second+after) ||
           !geometry_output_progression_safe(w.second,start,stride,last)))return false;
    }
    groups=unsigned(last)+1;return true;
}
// All columns, or nothing: any unproven column returns false and the caller
// walks every column exactly, including its reject reason.
inline bool prove_geometry_output_walk(const GeometryOutputWalk& w,unsigned columns,unsigned& total,unsigned& cross_columns)
{
    unsigned sum=0,crossing=0;
    for(unsigned column=0;column<columns;++column){
        const u128 route=spu_mpya(w.r[55],spu_a(spu_il(int(column)),w.r[54]),w.r[53]);
        const bool cross=spu_cgti(spu_a(spu_andi(route,15),w.r[48]),16)._u32[0]!=0;
        unsigned groups=0;
        if(!prove_geometry_output_column(w,column,cross,groups))return false;
        sum+=groups;crossing+=cross;
        if(sum>1048576)return false;
    }
    total=sum;cross_columns=crossing;return true;
}
// Integer encoders: cursor col*16+r52+r29 against xshw(r29)+r3.
inline GeometryOutputWalk geometry_integer_output_walk(const u128* r)
{
    GeometryOutputWalk w;w.r=r;
    w.cursor=spu_a(r[52],r[29])._u32[0];w.limit=spu_a(spu_xshw(r[29]),r[3])._u32[0];
    w.step=r[29]._u32[0];w.first=r[67]._u32[0];w.second=r[76]._u32[0];return w;
}
} // namespace buzz

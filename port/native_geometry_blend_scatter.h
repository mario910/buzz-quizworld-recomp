/* Complete remapped weighted scatter, preserving its two-pair input pipeline
 * and asymmetric destination prefetch/store order even with duplicate indices. */
#pragma once
#include "native_geometry_output.h"
#include "native_stagef_avx2.h"
#include <vector>
namespace buzz {
struct GeometryBlendRecord {u128 raw{},rotated{};uint32_t a=0,b=0;};
struct GeometryBlendScatter {
    uint32_t pairs=0,source=0;
    std::vector<GeometryBlendRecord> record;
};
inline bool read_geometry_blend_scatter(const spu_context& ctx,GeometryBlendScatter& output,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(ctx.ls!=ctx.ls_store)return reject("foreign LS");
    const uint32_t cursor=ctx.gpr[7]._u32[0],bytes=0u-cursor;
    if(int32_t(cursor)>=0 || (bytes&31) || bytes/32>4096)return reject("negative pair cursor");
    const uint32_t end=ctx.gpr[5]._u32[0],map=ctx.gpr[3]._u32[0],destination=ctx.gpr[4]._u32[0];
    if(end<bytes || (end&~15u)<0xe680 || uint64_t(end&~15u)+64>SPU_LS_SIZE)return reject("source end/lookahead");
    GeometryBlendScatter p;p.pairs=bytes/32;p.source=(end-bytes)&~15u;
    if(p.source<0xe680)return reject("source start");
    const uint64_t map_begin=map&~15u,map_end=((uint64_t(map)+(p.pairs+1)*4)&~15ull)+16;
    if(map_end>SPU_LS_SIZE)return reject("remap lookahead bounds");
    p.record.reserve(p.pairs+2);
    for(unsigned i=0;i<p.pairs+2;++i){
        GeometryBlendRecord r;r.raw=geometry_output_quad(ctx,(map+i*4)&~15u);
        r.rotated=spu_rotqby(r.raw,spu_il(i*4));
        if(i<=p.pairs){
            // LQD/STQD use the full packed preferred word before physical LS
            // wrapping. Unaligned halfword offsets and biased bases retain it.
            r.b=(destination+r.rotated._u32[0])&(SPU_LS_SIZE-16);
            r.a=(destination+(r.rotated._u32[0]>>16))&(SPU_LS_SIZE-16);
            if(i<p.pairs && (r.a<0xe680 || r.b<0xe680))return reject("stored destination below data");
            if(i<p.pairs)for(unsigned at:{r.a,r.b}){
                if(at<map_end && map_begin<uint64_t(at)+16)return reject("destination/remap alias");
            }
        }
        p.record.push_back(r);
    }
    output=std::move(p);return true;
}

inline void geometry_blend_entry_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[28] = spu_il(0);
    r[15] = spu_ls_read128(&ctx, r[5]._u32[0] + r[7]._u32[0]);
    r[16] = spu_ai(r[5], 16);
    r[17] = spu_ai(r[28], 4);
    r[30] = spu_ls_read128(&ctx, r[3]._u32[0] + r[28]._u32[0]);
    r[13] = spu_ai(r[7], 32);
    r[29] = spu_ls_read128(&ctx, r[16]._u32[0] + r[7]._u32[0]);
    r[18] = spu_ls_read128(&ctx, r[3]._u32[0] + r[17]._u32[0]);
    r[12] = spu_ori(r[13], 0);
    r[19] = spu_ls_read128(&ctx, r[16]._u32[0] + r[13]._u32[0]);
    r[25] = spu_and(r[15], r[6]);
    r[14] = spu_ls_read128(&ctx, r[5]._u32[0] + r[13]._u32[0]);
    r[2] = spu_rotqby(r[30], r[28]);
    r[27] = spu_and(r[29], r[6]);
    r[11] = spu_rotmi(r[2], -16);
    r[26] = spu_ls_read128(&ctx, r[4]._u32[0] + r[2]._u32[0]);
    r[24] = spu_ls_read128(&ctx, r[4]._u32[0] + r[11]._u32[0]);
    r[22] = spu_fma(r[27], r[8], r[26]);
}

inline void geometry_blend_final_abi(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[21] = spu_and(r[19], r[6]);
    r[10] = spu_rotqby(r[18], r[17]);
    r[13] = spu_ai(r[13], 32);
    r[17] = spu_ai(r[17], 4);
    r[9] = spu_rotqbii(r[11], 0);
    r[20] = spu_fma(r[25], r[8], r[24]);
    r[19] = spu_ls_read128(&ctx, r[16]._u32[0] + r[13]._u32[0]);
    r[11] = spu_rotmi(r[10], -16);
    r[23] = spu_ls_read128(&ctx, r[4]._u32[0] + r[10]._u32[0]);
    r[18] = spu_ls_read128(&ctx, r[3]._u32[0] + r[17]._u32[0]);
    r[7] = spu_ori(r[12], 0);
    spu_ls_write128(&ctx, r[4]._u32[0] + r[2]._u32[0], r[22]);
    r[25] = spu_and(r[14], r[6]);
    r[14] = spu_ls_read128(&ctx, r[5]._u32[0] + r[13]._u32[0]);
    r[12] = spu_ori(r[13], 0);
    r[24] = spu_ls_read128(&ctx, r[4]._u32[0] + r[11]._u32[0]);
    r[2] = spu_ori(r[10], 0);
    spu_ls_write128(&ctx, r[4]._u32[0] + r[9]._u32[0], r[20]);
    r[22] = spu_fma(r[21], r[8], r[23]);
}

inline void blend_geometry_scatter(spu_context& ctx,const GeometryBlendScatter& p)
{
    geometry_blend_entry_abi(ctx);auto& r=ctx.gpr;
    u128 masked_a=r[25],pending_b=r[22],loaded_a=r[24],raw_a=r[14],raw_b=r[19];
    const unsigned prefix=p.pairs-1;
    for(unsigned i=0;i<prefix;++i){
        const auto masked_next_b=spu_and(raw_b,r[6]);
        const auto result_a=spu_fma(masked_a,r[8],loaded_a);
        raw_b=geometry_output_quad(ctx,p.source+(i+2)*32+16);
        const auto loaded_next_b=geometry_output_quad(ctx,p.record[i+1].b);
        spu_ls_write128(&ctx,p.record[i].b,pending_b);
        masked_a=spu_and(raw_a,r[6]);
        raw_a=geometry_output_quad(ctx,p.source+(i+2)*32);
        // The next A load observes the current B store, but precedes A's
        // store. Moving either destination load across a store changes aliases.
        loaded_a=geometry_output_quad(ctx,p.record[i+1].a);
        spu_ls_write128(&ctx,p.record[i].a,result_a);
        pending_b=spu_fma(masked_next_b,r[8],loaded_next_b);
    }
    r[19]=raw_b;r[25]=masked_a;r[14]=raw_a;r[24]=loaded_a;r[22]=pending_b;
    r[13]=spu_ai(r[13],prefix*32);r[12]=r[13];r[17]=spu_ai(r[17],prefix*4);
    r[2]=p.record[prefix].rotated;r[11]=spu_rotmi(r[2],-16);r[18]=p.record[prefix+1].raw;
    geometry_blend_final_abi(ctx);
}
} // namespace buzz

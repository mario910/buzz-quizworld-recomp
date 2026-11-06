#pragma once
#include "native_geometry_host_cull_job.h"
#include <algorithm>
#include <cmath>

namespace buzz {
// Conservative whole-job clip rejection. Surviving or unbounded jobs retain
// the ordinary per-vertex projection and per-triangle culling. No writes to
// guest state, host streams or indices occur during this admission test.
inline bool geometry_host_trivially_outside(const spu_context& c,const GeometryJobEntry& e,
    const GeometryHostInputRecipe& recipe,const GeometryHostInputData& data)
{
    if(!data.ready || e.vertices<32 || e.vertices>4096 || recipe.vertices!=e.vertices ||
        recipe.fields>16 || recipe.blocks_per_field!=(e.vertices+7)/8 ||
        recipe.fields*recipe.blocks_per_field>GeometryHostInputData::capacity || (e.config[0]&16))return false;
    unsigned field=16;
    for(unsigned n=0;n<recipe.fields;++n)if(recipe.field[n].semantic==1)field=n;
    if(field==16)return false;
    GeometryHostClipParameters params;
    if(!read_geometry_host_clip_parameters(c,e,params))return false;
    auto ordinary=[](uint32_t bits){return (bits&0x7fffffff)<=0x4e800000u;};
    auto number=[](uint32_t bits){float value;memcpy(&value,&bits,4);return double(value);};
    for(const auto& row:params.row)for(auto bits:row)if(!ordinary(bits))return false;
    for(unsigned axis=0;axis<3;++axis)
        if(!ordinary(params.minimum[axis]) || !ordinary(params.maximum[axis]))return false;
    __m256 lo[3],hi[3];
    for(unsigned axis=0;axis<3;++axis){lo[axis]=_mm256_set1_ps(0x1p30f);hi[axis]=_mm256_set1_ps(-0x1p30f);}
    v8 outside=_mm256_setzero_si256();
    for(unsigned block=0;block<recipe.blocks_per_field;++block) {
        const auto& value=data.at(recipe,field,block);
        for(unsigned axis=0;axis<3;++axis) {
            const auto bits=value.component[axis];
            outside=_mm256_or_si256(outside,_mm256_cmpgt_epi32(
                _mm256_and_si256(bits,v8_set(0x7fffffff)),v8_set(0x4e800000)));
            const auto v=_mm256_castsi256_ps(bits);
#if defined(__aarch64__)
            lo[axis]={vminq_f32(lo[axis].lo,v.lo),vminq_f32(lo[axis].hi,v.hi)};
            hi[axis]={vmaxq_f32(hi[axis].lo,v.lo),vmaxq_f32(hi[axis].hi,v.hi)};
#else
            lo[axis]=_mm256_min_ps(lo[axis],v);hi[axis]=_mm256_max_ps(hi[axis],v);
#endif
        }
    }
    if(_mm256_movemask_ps(_mm256_castsi256_ps(outside)))return false;
    double minimum[3],maximum[3];
    for(unsigned axis=0;axis<3;++axis) {
        uint32_t a[8],b[8];
        _mm256_storeu_si256(reinterpret_cast<v8*>(a),_mm256_castps_si256(lo[axis]));
        _mm256_storeu_si256(reinterpret_cast<v8*>(b),_mm256_castps_si256(hi[axis]));
        minimum[axis]=number(a[0]);maximum[axis]=number(b[0]);
        for(unsigned lane=1;lane<8;++lane) {
            minimum[axis]=std::min(minimum[axis],number(a[lane]));
            maximum[axis]=std::max(maximum[axis],number(b[lane]));
        }
    }
    double low[4],high[4];
    for(unsigned row=0;row<4;++row) {
        low[row]=high[row]=number(params.row[row][3]);
        double magnitude=std::abs(low[row]);
        for(unsigned axis=0;axis<3;++axis) {
            const double coefficient=number(params.row[row][axis]);
            const double a=coefficient*minimum[axis],b=coefficient*maximum[axis];
            low[row]+=std::min(a,b);high[row]+=std::max(a,b);
            magnitude+=std::max(std::abs(a),std::abs(b));
        }
        // Three RZ float FMAs, including flushed operands/results. The bound
        // exceeds gamma(3) by >5x; double endpoint rounding is much smaller.
        const double error=2e-6*magnitude+1e-20;
        low[row]-=error;high[row]+=error;
    }
    double wmin,wmax;
    if(low[3]>=0x1p-30 && high[3]<=0x1p30){wmin=low[3];wmax=high[3];}
    else if(high[3]<=-0x1p-30 && low[3]>=-0x1p30){wmin=-high[3];wmax=-low[3];}
    else return false; // Mixed/zero/extreme W: no projective interval proof.
    for(unsigned axis=0;axis<3;++axis) {
        double a,b;
        if(low[axis]>=0){a=low[axis]/wmax;b=high[axis]/wmin;}
        else if(high[axis]<=0){a=low[axis]/wmin;b=high[axis]/wmax;}
        else {a=low[axis]/wmin;b=high[axis]/wmin;}
        // Deliberately wide allowance for the original SPU FREST/FI/NR
        // reciprocal (and native division). Normal W scale invariance and
        // the mantissa sweep are checked by the coarse-clip test.
        const double error=std::max(std::abs(a),std::abs(b))/256.0+1e-20;
        if(b+error<number(params.minimum[axis]) || a-error>number(params.maximum[axis])) {
            GeometryHostCullParameters cull;
            return read_geometry_host_cull_parameters(c,e,cull);
        }
    }
    return false;
}
}

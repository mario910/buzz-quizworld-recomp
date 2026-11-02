/* EA03 time/block selection and GET recipe. No SPU register/ABI state. */
#pragma once
#include "native_animation_host_quaternions.h"

namespace buzz {
struct AnimationClipTimeInput {
    AnimationByteView header;
    uint32_t ea=0,lsa=0,time=0;
};
struct AnimationClipTimeSelection {
    uint32_t ea=0,lsa=0,bytes=0,fraction=0;
    unsigned block=0,start_frame=0,interior_frames=0,frame=0;
};
inline uint32_t animation_positive_float_integer(uint32_t bits)
{
    const unsigned exponent=(bits>>23)&255;
    if(bits>>31||exponent<127)return 0;
    if(exponent>=159)return ~uint32_t(0);
    const uint32_t mantissa=(bits&0x7fffff)|0x800000;
    return exponent>=150?mantissa<<(exponent-150):mantissa>>(150-exponent);
}
inline bool select_animation_clip_times8(const AnimationClipTimeInput* input,unsigned lanes,
    AnimationClipTimeSelection* output,const char** reason=nullptr)
{
    auto reject=[&](const char* message){if(reason)*reason=message;return false;};
    auto be16=[](const uint8_t* p){return unsigned(p[0])<<8|p[1];};
    auto be32=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    if(lanes>8)return reject("clip time lane count");
    if(!lanes){if(reason)*reason=nullptr;return true;}
    if(!input||!output)return reject("clip time input/output pointer");
    AnimationClipTimeSelection plan[8];
    unsigned blocks[8]{},time_table[8]{},data_table[8]{};
    alignas(32) uint32_t times[8]{},rates[8]{},starts[8]{},frames[8]{};
    for(unsigned lane=0;lane<lanes;++lane) {
        const auto& clip=input[lane];const auto h=clip.header;
        if(!h.data||h.bytes<64||be32(h.data)!=0x45413033||be16(h.data+12)!=h.bytes)
            return reject("EA03 timing header span/version");
        if((clip.ea|clip.lsa)&15||!clip.ea||uint64_t(clip.lsa)+h.bytes>0x40000)
            return reject("clip header address");
        const unsigned count=be16(h.data+18),samples=be16(h.data+16);
        const uint32_t dt=be32(h.data+52),tt=be32(h.data+56),rate=be32(h.data+8);
        if(count<2||samples<2||!dt||!tt||uint64_t(dt)+52+uint64_t(count)*8>h.bytes||
           uint64_t(tt)+56+uint64_t(count)*4>h.bytes)return reject("clip block tables");
        if((rate>>31)||rate<0x00800000||rate>=0x7f800000)return reject("clip sample rate");
        const unsigned data=52+dt,table=56+tt;
        unsigned expected_start=0;
        for(unsigned block=0;block<count;++block) {
            const unsigned start=be16(h.data+table+block*4),width=be16(h.data+table+block*4+2);
            if(start!=expected_start||width>128)return reject("clip frame intervals");
            if(block+1==count) {
                if(width||start!=samples-1)return reject("clip terminal interval");
            } else expected_start=start+width+1;
        }
        blocks[lane]=count;time_table[lane]=table;data_table[lane]=data;
        times[lane]=clip.time;rates[lane]=rate;
    }
    const v8 raw=v8_fm(_mm256_load_si256(reinterpret_cast<const v8*>(times)),
        _mm256_load_si256(reinterpret_cast<const v8*>(rates)));
    const v8 zero=_mm256_setzero_si256();
    const v8 nonnegative=_mm256_blendv_epi8(raw,zero,v8_fcgt(zero,raw));
    alignas(32) uint32_t sample[8];_mm256_store_si256(reinterpret_cast<v8*>(sample),nonnegative);
    for(unsigned lane=0;lane<lanes;++lane) {
        const auto& clip=input[lane];const auto* h=clip.header.data;
        // The covered clip domain has at most 65535 samples. Larger clocks
        // use the lifted whole job rather than reproducing wrapped indices.
        const unsigned integer=animation_positive_float_integer(sample[lane]);
        if(integer>65535)return reject("uncovered clip clock range");
        unsigned low=0,high=blocks[lane]-1;
        while(low+1<high) {
            const unsigned middle=(low+high)/2;
            if(be16(h+time_table[lane]+middle*4)<=integer)low=middle;
            else high=middle;
        }
        auto& selected=plan[lane];selected.block=low;
        selected.start_frame=be16(h+time_table[lane]+low*4);
        selected.interior_frames=be16(h+time_table[lane]+low*4+2);
        starts[lane]=selected.start_frame;
        const auto* record=h+data_table[lane]+low*8;
        const uint32_t encoded_size=be32(record),offset=be32(record+4);
        if(encoded_size&~0x7fffu||!encoded_size||encoded_size>16384||(encoded_size|offset)&15||
           offset<clip.header.bytes||uint64_t(clip.ea)+offset+encoded_size>0x100000000ull)return reject("clip body GET span/flags");
        selected.ea=clip.ea+offset;selected.bytes=encoded_size;
        const uint32_t table_lsa=clip.lsa+data_table[lane];
        selected.lsa=table_lsa+((selected.ea-table_lsa)&127);
        if((selected.lsa&15)||uint64_t(selected.lsa)+selected.bytes>0x40000)
            return reject("clip body LS span");
    }
    const v8 start=_mm256_castps_si256(_mm256_cvtepi32_ps(_mm256_load_si256(reinterpret_cast<const v8*>(starts))));
    const v8 local=v8_fs(nonnegative,start);
    alignas(32) uint32_t local_sample[8];_mm256_store_si256(reinterpret_cast<v8*>(local_sample),local);
    for(unsigned lane=0;lane<lanes;++lane) {
        const unsigned integer=animation_positive_float_integer(local_sample[lane]);
        plan[lane].frame=integer>plan[lane].interior_frames?plan[lane].interior_frames:integer;
        frames[lane]=integer>plan[lane].interior_frames?0:integer;
    }
    const v8 frame=_mm256_castps_si256(_mm256_cvtepi32_ps(_mm256_load_si256(reinterpret_cast<const v8*>(frames))));
    alignas(32) uint32_t fraction[8];_mm256_store_si256(reinterpret_cast<v8*>(fraction),v8_fs(local,frame));
    for(unsigned lane=0;lane<lanes;++lane) {
        plan[lane].fraction=animation_positive_float_integer(local_sample[lane])>plan[lane].interior_frames?0x3f800000:fraction[lane];
        output[lane]=plan[lane];
    }
    if(reason)*reason=nullptr;return true;
}
} // namespace buzz

/* Packed animation keys -> eight host quaternions. No SPU ABI or scratch.
 * The job frontend supplies bounded raw streams and owns the output arrays. */
#pragma once
#include "native_geometry_host_data.h"

namespace buzz {
using AnimationHostQuaternion8 = GeometryHostSoA8;
struct AnimationByteView { const uint8_t* data=nullptr;size_t bytes=0; };
struct AnimationKeyTracks {
    AnimationByteView key_bits,first,last,interior;
    unsigned first_bit=0,interior_frames=0,tracks=0,frame=0;
    uint32_t fraction=0;
};
using AnimationQuaternionTracks = AnimationKeyTracks;

inline v8 animation_add8(v8 a,v8 b)
{
    return v8_finish(_mm256_add_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b)),a,b,a,0);
}
inline bool animation_read_key(AnimationByteView stream,unsigned index,uint64_t& key)
{
    const uint64_t at=uint64_t(index)*6;
    if(at+6>stream.bytes)return false;
    key=0;for(unsigned byte=0;byte<6;++byte)key=(key<<8)|stream.data[at+byte];
    return true;
}
inline uint64_t animation_key_bits64(AnimationByteView stream,uint64_t first,unsigned count)
{
    const size_t at=size_t(first/8),available=stream.bytes-at;
    const unsigned shift=first%8;
    uint64_t word=0;memcpy(&word,stream.data+at,available<8?available:8);
    word=__builtin_bswap64(word)<<shift;
    if(shift&&available>8)word|=uint64_t(stream.data[at+8])>>(8-shift);
    return count==64?word:word&(~uint64_t(0)<<(64-count));
}
inline AnimationHostQuaternion8 decode_animation_host_quaternions8(const uint64_t (&packed)[8])
{
    alignas(32) uint32_t field[3][8],index[8];
    for(unsigned lane=0;lane<8;++lane) {
        field[0][lane]=uint32_t(packed[lane]>>32);
        field[1][lane]=uint32_t(packed[lane]>>17)&0x7fff;
        field[2][lane]=uint32_t(packed[lane]>>2)&0x7fff;index[lane]=packed[lane]&3;
    }
    v8 decoded[3];const v8 one=v8_set(0x3f800000);
    for(unsigned component=0;component<3;++component) {
        const auto integer=_mm256_load_si256(reinterpret_cast<const v8*>(field[component]));
        const v8 scaled=v8_fm(_mm256_castps_si256(_mm256_cvtepi32_ps(integer)),v8_set(0x38000000));
        decoded[component]=v8_fma(scaled,v8_set(0x3fb504f3),v8_set(0xbf3504f3));
    }
    v8 squared=v8_fnms(decoded[0],decoded[0],one);
    squared=v8_fnms(decoded[1],decoded[1],squared);squared=v8_fnms(decoded[2],decoded[2],squared);
    const v8 estimate=v8_fi(squared,v8_frsqest(squared));
    const v8 product=v8_fm(squared,estimate);
    const v8 correction=v8_fnms(product,estimate,one);
    const v8 refined=v8_fma(v8_fm(estimate,v8_set(0x3f000000)),correction,estimate);
    const v8 omitted=v8_fm(refined,squared);
    const v8 selector=_mm256_load_si256(reinterpret_cast<const v8*>(index));
    AnimationHostQuaternion8 result;
    for(unsigned component=0;component<4;++component) {
        const v8 is_omitted=_mm256_cmpeq_epi32(selector,v8_set(component));
        v8 value=decoded[component==3?2:component];
        if(component) value=_mm256_blendv_epi8(value,decoded[component-1],
            _mm256_cmpgt_epi32(v8_set(component),selector));
        result.component[component]=_mm256_blendv_epi8(value,omitted,is_omitted);
    }
    return result;
}
inline bool decode_animation_host_static_quaternions(AnimationByteView stream,unsigned count,
    AnimationHostQuaternion8* output)
{
    if(uint64_t(count)*6>stream.bytes)return false;
    for(unsigned first=0;first<count;first+=8) {
        uint64_t packed[8];
        for(unsigned lane=0;lane<8;++lane) {
            packed[lane]=0x400080010003ull;
            if(first+lane<count&&!animation_read_key(stream,first+lane,packed[lane]))return false;
        }
        output[first/8]=decode_animation_host_quaternions8(packed);
    }
    return true;
}
inline v8 animation_sine8(v8 angle)
{
    const v8 square=v8_fm(angle,angle);
    v8 value=v8_fma(v8_set(0x3638b94f),square,v8_set(0xb9500c03));
    value=v8_fma(value,square,v8_set(0x3c088887));
    value=v8_fma(value,square,v8_set(0xbe2aaaab));
    value=v8_fma(value,square,v8_set(0x3f800000));return v8_fm(value,angle);
}
inline AnimationHostQuaternion8 interpolate_animation_host_quaternions8(
    const AnimationHostQuaternion8& first,const AnimationHostQuaternion8& last,v8 weight)
{
    const v8 one=v8_set(0x3f800000),zero=_mm256_setzero_si256();
    v8 dot=v8_fm(first.component[0],last.component[0]);
    for(unsigned component=1;component<4;++component)dot=v8_fma(first.component[component],last.component[component],dot);
    const v8 sign=_mm256_blendv_epi8(one,v8_set(0xbf800000),v8_fcgt(zero,dot));
    const v8 cosine=v8_fm(dot,sign),near=v8_fcgt(cosine,v8_set(0x3f7fbe77));
    v8 first_weight=v8_fs(one,weight),last_weight=weight;
    // The original selects linear weights for near-parallel endpoints. Its
    // discarded acos/sine intermediates need not be evaluated on those lanes.
    if(_mm256_movemask_ps(_mm256_castsi256_ps(near))!=0xff) {
        const v8 x=_mm256_blendv_epi8(cosine,v8_set(0x3f000000),near);
        v8 polynomial=v8_fm(x,v8_set(0x3d4d8392));
        polynomial=v8_fnms(x,polynomial,v8_set(0x3db63a9e));
        polynomial=v8_fnms(x,polynomial,v8_set(0x3e5bbfca));
        polynomial=v8_fnms(x,polynomial,v8_set(0x3fc90fda));
        const v8 inverse_root=v8_fi(v8_fs(one,x),v8_frsqest(v8_fs(one,x)));
        const v8 angle=v8_fm(polynomial,v8_fnms(inverse_root,x,inverse_root));
        const v8 sine=animation_sine8(angle),inverse_sine=v8_fi(sine,v8_frest(sine));
        const v8 a=v8_fm(animation_sine8(v8_fm(first_weight,angle)),inverse_sine);
        const v8 b=v8_fm(animation_sine8(v8_fm(weight,angle)),inverse_sine);
        first_weight=_mm256_blendv_epi8(a,first_weight,near);last_weight=_mm256_blendv_epi8(b,weight,near);
    }
    AnimationHostQuaternion8 result;
    for(unsigned component=0;component<4;++component)
        result.component[component]=v8_fma(v8_fm(last.component[component],sign),last_weight,
            v8_fm(first.component[component],first_weight));
    return result;
}

struct AnimationTrackSelection8 {
    AnimationByteView before_stream[8],after_stream[8];
    unsigned before_index[8]{},after_index[8]{};
    v8 weight;
};
inline bool animation_track_recipe_valid(const AnimationKeyTracks& recipe,unsigned stride)
{
    // Original clips expose four 32-bit masks. Frame N+1 is the final
    // endpoint, reached from integer frame N plus its fractional part.
    return recipe.first_bit<=7&&recipe.interior_frames<=128&&
        recipe.frame<=recipe.interior_frames&&
        uint64_t(recipe.first_bit)+uint64_t(recipe.tracks)*recipe.interior_frames<=uint64_t(recipe.key_bits.bytes)*8&&
        uint64_t(recipe.tracks)*stride<=recipe.first.bytes&&uint64_t(recipe.tracks)*stride<=recipe.last.bytes;
}
// Track/time ordering and interpolation times are independent of key format.
inline void select_animation_host_tracks8(const AnimationKeyTracks& recipe,unsigned first,
    unsigned& key_prefix,AnimationTrackSelection8& selection)
{
    alignas(32) uint32_t before_delta[8]{},span[8];
    for(unsigned lane=0;lane<8;++lane) {
        span[lane]=1;
        const unsigned track=first+lane;if(track>=recipe.tracks)continue;
        unsigned previous=0,next=recipe.interior_frames+1,keys=0,previous_key=0,next_key=0;
        bool found_previous=false,found_next=false;
        const uint64_t bit_base=recipe.first_bit+uint64_t(track)*recipe.interior_frames;
        for(unsigned frame=0;frame<recipe.interior_frames;frame+=64) {
            const unsigned count=recipe.interior_frames-frame<64?recipe.interior_frames-frame:64;
            const uint64_t bits=animation_key_bits64(recipe.key_bits,bit_base+frame,count);
            const unsigned prefix=recipe.frame<=frame?0:recipe.frame-frame<count?recipe.frame-frame:count;
            const uint64_t mask=prefix==64?~uint64_t(0):prefix?~uint64_t(0)<<(64-prefix):0;
            const uint64_t before_bits=bits&mask,after_bits=bits&~mask;
            const unsigned before_keys=__builtin_popcountll(before_bits);
            if(before_bits){previous=frame+64-__builtin_ctzll(before_bits);previous_key=key_prefix+keys+before_keys-1;found_previous=true;}
            if(after_bits&&!found_next){next=frame+unsigned(__builtin_clzll(after_bits))+1;next_key=key_prefix+keys+before_keys;found_next=true;}
            keys+=__builtin_popcountll(bits);
        }
        selection.before_stream[lane]=found_previous?recipe.interior:recipe.first;
        selection.after_stream[lane]=found_next?recipe.interior:recipe.last;
        selection.before_index[lane]=found_previous?previous_key:track;
        selection.after_index[lane]=found_next?next_key:track;
        key_prefix+=keys;before_delta[lane]=recipe.frame-previous;span[lane]=next-previous;
    }
    const v8 length=_mm256_castps_si256(_mm256_cvtepi32_ps(_mm256_load_si256(reinterpret_cast<const v8*>(span))));
    v8 inverse=v8_fi(length,v8_frest(length));
    inverse=v8_fma(v8_fnms(length,inverse,v8_set(0x3f800000)),inverse,inverse);
    const v8 elapsed=_mm256_castps_si256(_mm256_cvtepi32_ps(_mm256_load_si256(reinterpret_cast<const v8*>(before_delta))));
    selection.weight=v8_fm(animation_add8(v8_set(recipe.fraction),elapsed),inverse);
}

// Selection and decoding happen once into job-owned host output. A rejection
// may leave partial host data, which the frontend discards before job mutation.
inline bool sample_animation_host_quaternions(const AnimationQuaternionTracks& recipe,
    AnimationHostQuaternion8* output)
{
    if(!animation_track_recipe_valid(recipe,6))return false;
    unsigned key_prefix=0;
    for(unsigned first=0;first<recipe.tracks;first+=8) {
        AnimationTrackSelection8 selection;
        select_animation_host_tracks8(recipe,first,key_prefix,selection);
        uint64_t before[8],after[8];
        for(unsigned lane=0;lane<8;++lane) {
            before[lane]=after[lane]=0x400080010003ull;
            if(first+lane>=recipe.tracks)continue;
            if(!animation_read_key(selection.before_stream[lane],selection.before_index[lane],before[lane])||
               !animation_read_key(selection.after_stream[lane],selection.after_index[lane],after[lane]))return false;
        }
        output[first/8]=interpolate_animation_host_quaternions8(
            decode_animation_host_quaternions8(before),decode_animation_host_quaternions8(after),selection.weight);
    }
    return true;
}
} // namespace buzz

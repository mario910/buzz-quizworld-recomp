/* EA03 raw clip storage -> bounded host sampling recipes.
 * Offsets and IDs describe clip data, not SPU registers or call frames. */
#pragma once
#include "native_animation_host_curve_keys.h"

namespace buzz {
struct AnimationClipInput {
    unsigned joints=0,frames=0,blocks=0;
    unsigned static_count[4]{},animated_count[4]{};
    unsigned published_tail_bytes=0;
    AnimationByteView static_ids[4],animated_ids[4],static_values[4];
    AnimationKeyTracks animated[4]; // quaternion, translation, scale, curve
};
inline bool read_animation_clip_input(AnimationByteView header,AnimationByteView block,AnimationByteView published_tail,
    unsigned curve_capacity,unsigned interior_frames,unsigned frame,uint32_t fraction,AnimationClipInput& output,
    const char** reason=nullptr)
{
    auto reject=[&](const char* message){if(reason)*reason=message;return false;};
    auto be16=[](const uint8_t* p){return unsigned(p[0])<<8|p[1];};
    auto be32=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    auto slice=[](AnimationByteView source,size_t offset,size_t size){return AnimationByteView{source.data+offset,size};};
    if(!header.data||header.bytes<96||be32(header.data)!=0x45413033||be16(header.data+12)!=header.bytes)
        return reject("EA03 header span/version");
    if(curve_capacity>256)return reject("curve capacity");
    if(!block.data||block.bytes<16||interior_frames>128||frame>interior_frames)
        return reject("clip block/frame span");
    // At most three stored curve slots, each with <=128 interior keys,
    // plus a four-slot endpoint tail: 3*128*4 + 12 bytes.
    if(published_tail.bytes>1548||(published_tail.bytes&&published_tail.data!=block.data+block.bytes))
        return reject("published tail input span");
    if(be16(header.data+38))return reject("uncovered compressed streams");
    AnimationClipInput plan;
    plan.joints=be16(header.data+14);plan.frames=be16(header.data+16);plan.blocks=be16(header.data+18);
    if(!plan.joints||plan.joints>256||!plan.frames||!plan.blocks)return reject("clip counts");
    constexpr unsigned stride[]={6,12,12,4};
    size_t ids=96;
    for(unsigned kind=0;kind<4;++kind) {
        const unsigned n=be16(header.data+22+kind*2),group=kind?4:8;
        if(n>256)return reject("static track count");
        plan.static_count[kind]=n;
        const size_t stored=(n+group-1)&~size_t(group-1),bytes=stored*2;
        if(ids+bytes>header.bytes)return reject("static ID span");
        plan.static_ids[kind]=slice(header,ids,bytes);ids+=bytes;
        if(n) {
            const size_t field=60+kind*4,relative=be32(header.data+field),at=field+relative;
            if(!relative||at>header.bytes||size_t(n)*stride[kind]>header.bytes-at)
                return reject("static value span");
            plan.static_values[kind]=slice(header,at,size_t(n)*stride[kind]);
        }
    }
    unsigned animated_total=0;
    for(unsigned kind=0;kind<4;++kind) {
        const unsigned n=be16(header.data+30+kind*2);
        if(n>256)return reject("animated track count");
        plan.animated_count[kind]=n;animated_total+=n;
        const size_t bytes=((n+3)&~size_t(3))*2;
        if(ids+bytes>header.bytes)return reject("animated ID span");
        plan.animated_ids[kind]=slice(header,ids,bytes);ids+=bytes;
    }
    for(unsigned kind=0;kind<4;++kind) {
        const unsigned capacity=kind==3?curve_capacity:plan.joints;
        for(unsigned i=0;i<plan.static_count[kind];++i)
            if(be16(plan.static_ids[kind].data+i*2)>=capacity)return reject("static channel ID");
        if(kind!=3)for(unsigned i=0;i<plan.animated_count[kind];++i)
            if(be16(plan.animated_ids[kind].data+i*2)>=capacity)return reject("animated joint ID");
    }
    size_t first[4],interior[4],last[4],cursor=16;
    size_t first_bytes[4],interior_bytes[4],last_bytes[4];
    for(unsigned kind=0;kind<4;++kind) {
        const size_t bytes=be16(block.data+kind*2);
        if(cursor+bytes>block.bytes)return reject("first endpoint span");
        if(bytes<size_t(plan.animated_count[kind])*stride[kind])return reject("first endpoint count");
        first[kind]=cursor;first_bytes[kind]=bytes;cursor+=bytes;
    }
    const size_t bits=cursor,bit_bytes=(size_t(animated_total)*interior_frames+7)/8;
    if(cursor+bit_bytes>block.bytes)return reject("key bit span");
    cursor+=bit_bytes;
    for(unsigned kind=0;kind<4;++kind) {
        if(kind==3)cursor=(cursor+3)&~size_t(3);
        const size_t bytes=be16(block.data+8+kind*2);
        if(cursor+bytes>block.bytes)return reject("interior value span");
        interior[kind]=cursor;interior_bytes[kind]=bytes;cursor+=bytes;
    }
    cursor=(cursor+15)&~size_t(15);
    if(cursor+16>block.bytes)return reject("last endpoint header span");
    const auto* end=block.data+cursor;cursor+=16;
    for(unsigned kind=0;kind<4;++kind) {
        const size_t bytes=be16(end+kind*2);
        if(cursor+bytes>block.bytes)return reject("last endpoint span");
        if(bytes<size_t(plan.animated_count[kind])*stride[kind])return reject("last endpoint count");
        last[kind]=cursor;last_bytes[kind]=bytes;cursor+=bytes;
    }
    size_t preceding_bits=0;
    for(unsigned kind=0;kind<4;++kind) {
        const size_t at=bits+preceding_bits/8;
        // Stored FLOAT1 tail IDs can own published alignment words. Their
        // endpoint values may alias bytes after the fetched block. Such bytes
        // are explicit observable job input, supplied separately by the caller.
        const size_t available=block.bytes+(kind==3?published_tail.bytes:0);
        plan.animated[kind]={slice(block,at,kind==3?available-at:(preceding_bits%8+size_t(plan.animated_count[kind])*interior_frames+7)/8),
            slice(block,first[kind],kind==3?available-first[kind]:first_bytes[kind]),
            slice(block,last[kind],kind==3?available-last[kind]:last_bytes[kind]),
            slice(block,interior[kind],kind==3?available-interior[kind]:interior_bytes[kind]),
            unsigned(preceding_bits%8),interior_frames,plan.animated_count[kind],frame,fraction};
        preceding_bits+=size_t(plan.animated_count[kind])*interior_frames;
    }
    if(plan.animated_count[3]) {
        AnimationHostCurveKeyPlan ownership;
        if(!plan_animation_host_curve_keys(plan.animated_ids[3],plan.animated_count[3],curve_capacity,ownership))
            return reject("curve channel mapping");
        auto recipe=plan.animated[3];recipe.tracks=ownership.tracks;
        if(!animation_track_recipe_valid(recipe,4))return reject("stored curve input bounds");
        size_t required=last[3]+ownership.tracks*4;
        const size_t bit_end=size_t(recipe.key_bits.data-block.data)+(recipe.first_bit+size_t(ownership.tracks)*interior_frames+7)/8;
        if(bit_end>required)required=bit_end;
        unsigned prefix=0;
        for(unsigned first_track=0;first_track<ownership.tracks;first_track+=8) {
            AnimationTrackSelection8 selected;select_animation_host_tracks8<false>(recipe,first_track,prefix,selected);
            for(unsigned lane=0;lane<8&&first_track+lane<ownership.tracks;++lane)if(ownership.publish[first_track+lane]) {
                const AnimationByteView streams[]={selected.before_stream[lane],selected.after_stream[lane]};
                const unsigned indices[]={selected.before_index[lane],selected.after_index[lane]};
                for(unsigned endpoint=0;endpoint<2;++endpoint) {
                    const size_t bytes=(size_t(indices[endpoint])+1)*4;
                    if(bytes>streams[endpoint].bytes)return reject("published curve key span");
                    const size_t end=size_t(streams[endpoint].data-block.data)+bytes;
                    if(end>required)required=end;
                }
            }
        }
        if(required>block.bytes)plan.published_tail_bytes=unsigned(required-block.bytes);
    }
    output=plan;if(reason)*reason=nullptr;return true;
}
} // namespace buzz

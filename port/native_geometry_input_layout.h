/* Pre-mutation proof for the two AC68 ->93F0 stream calls in the pinned job.
 * Scratch tables must not reach either callee's saved control registers. */
#pragma once
#include "native_geometry_job_input.h"
namespace buzz {
inline bool read_geometry_input_layout(const spu_context& c,const GeometryJobEntry& e,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    const bool secondary=e.input[11].size_flags!=0;
    if((e.config[0]&16) && secondary)return reject("input secondary assertion");
    if(!e.vertices){if(e.frame_lsa<0xe680+576)return reject("empty input protected frame");return true;}
    const unsigned tables=e.uniform_tables(),stride=((e.vertices+7)&~7u)*16;
    unsigned next=0,needed=0,padding=0,saved=576;
    auto reserve=[&](unsigned semantic){if(next+1>needed)needed=next+1;if(semantic)++next;};
    for(unsigned stream=0;stream<(secondary?2u:1u);++stream){
        const unsigned format=e.config[2+stream];
        if(format<6){
            static constexpr unsigned rows[]={1,3,3,4,3,4};for(unsigned row=1;row<=rows[format];++row)reserve(row);
            if(format>=4 && padding<32)padding=32;
        }else{
            saved=1920; // AC68's128 bytes plus the descriptor/codec0x700 window.
            // Custom descriptor rows share the same scratch table allocator.
            const auto& d=e.input[19+stream];
            if((d.lsa&15) || d.size<2 || d.lsa>SPU_LS_SIZE-16)return reject("input descriptor span");
            const unsigned count=c.ls[d.lsa],width=c.ls[d.lsa+1];
            const unsigned metadata_bytes=count?8+count*8:2;
            // The declaration covers the8-byte header and its8-byte rows.
            // Codecs may speculatively read a further quad from actual LS;
            // that is not an additional declared descriptor row.
            if(count>16 || !width || metadata_bytes>d.size || uint64_t(d.lsa)+metadata_bytes>SPU_LS_SIZE)return reject("input descriptor header");
            for(unsigned i=0;i<count;++i){const auto* a=c.ls+d.lsa+8+i*8;
                if(a[1]<1 || a[1]>9 || a[2]>4 || !a[4] || unsigned(a[0])+a[4]>width)return reject("input descriptor row");
                if(!(e.config[0]&16) || a[3]==1)reserve(a[3]);
            }
            // Typed codecs retain up to four extra rows in their LS contract.
            if(count && padding<64)padding=64;
        }
    }
    if(needed>tables)return reject("builtin input slot capacity");
    const uint64_t bytes=uint64_t(tables)*stride+padding,end=uint64_t(e.scratch_lsa)+bytes;
    if(bytes>e.scratch_bytes || end>SPU_LS_SIZE)return reject("builtin input scratch capacity");
    if(e.frame_lsa<saved || end>e.frame_lsa-saved)return reject("builtin input saved-control overlap");
    return true;
}
}

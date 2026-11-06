#ifndef BUZZ_RENDERER_STATE_PACKETS_H
#define BUZZ_RENDERER_STATE_PACKETS_H
#include "rsx_commands.h"
#include "rsx_dispatch.h"
#include <string.h>

/* Only pure register state: never include a draw, notification or fence.
 * Bound the scratch storage even for non-incrementing packets. */
static inline int buzz_state_packet_eligible(u32 subchannel,u32 type,u32 method,u32 count) {
    if(subchannel!=0 || (type!=0 && type!=2) || !count || count>33 || (method&3))return 0;
    u32 limit=0;
    if(method>=0x1efc && method<=0x1f7c)limit=0x1f80;
    else if(method>=0x1740 && method<=0x177c)limit=0x1780;
    else if(method>=0x1680 && method<=0x16bc)limit=0x16c0;
    else if(method==0x08e4 || method==0x1714)limit=method+4;
    return limit && (type==2 || count<=(limit-method)/4);
}

/* Host-endian words, in FIFO order. The two consumers can have different
 * LOAD values and limits; preserve both independently, including u32 wrap.
 * A null dispatch means the draw engine has not been initialized yet. */
static inline void buzz_state_packet_apply(rsx_dispatch* rsx,rsx_state* state,
    u32 method,u32 step,const u32* words,u32 count) {
    for(u32 i=0;i<count;++i,method+=step) {
        const u32 value=words[i];
        if(rsx) {
            rsx->seen[method/4]++;
            rsx->regs[method/4]=value;
        }
        if(method>=0x1740 && method<=0x177c) {
            rsx_vertex_attrib* attr=&state->vertex_attribs[(method-0x1740)/4];
            attr->type=value&15;attr->size=(value>>4)&15;
            attr->stride=(value>>8)&255;attr->frequency=value>>16;
            attr->enabled=attr->size!=0;attr->format=value;
            state->vertex_dirty=1;
            continue;
        }
        if(method>=0x1680 && method<=0x16bc) {
            state->vertex_attribs[(method-0x1680)/4].offset=value;
            state->vertex_dirty=1;
            continue;
        }
        if(method==0x08e4) {
            state->shader_program=value;state->fragment_program_addr=value&~3u;
            state->shader_dirty=1;
            continue;
        }
        if(method==0x1714) {
            /* The SDK has no vertex cache to invalidate. Its register/seen
             * writes above still occur. Preserve unknown-method diagnostics
             * in debug builds (the FIFO ignores their return value). */
#ifndef NDEBUG
            rsx_process_method(state,method,value);
#endif
            continue;
        }
        if(method==0x1efc) { state->transform_constant_load=value; continue; }
        const u32 word=(method-0x1f00)/4, lane=word&3;
        if(rsx) {
            const u32 slot=rsx->regs[0x1efc/4]+(word>>2);
            if(slot<RSX_DSP_NUM_CONSTANTS)rsx->constants[slot][lane]=value;
        }
        const u32 slot=state->transform_constant_load+(word>>2);
        if(slot<RSX_MAX_VERTEX_CONSTANTS) {
            memcpy(&state->vertex_constants[slot][lane],&value,sizeof value);
            if(!state->vertex_constants_dirty) {
                state->vertex_constants_lo=slot;
                state->vertex_constants_hi=slot;
                state->vertex_constants_dirty=1;
            } else {
                if(slot<state->vertex_constants_lo)state->vertex_constants_lo=slot;
                if(slot>state->vertex_constants_hi)state->vertex_constants_hi=slot;
            }
        }
    }
}
#endif

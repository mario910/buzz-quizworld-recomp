#ifndef BUZZ_RENDERER_STATE_PACKETS_H
#define BUZZ_RENDERER_STATE_PACKETS_H
#include "rsx_commands.h"
#include "rsx_dispatch.h"
#include <string.h>

/* Only pure register state: never include a draw, notification or fence.
 * Bound the scratch storage even for non-incrementing packets. */
static inline int buzz_state_packet_eligible(u32 subchannel,u32 type,u32 method,u32 count) {
    if(subchannel!=0 || (type!=0 && type!=2) || !count || count>33 || (method&3))return 0;
    if (type == 0 && method == NV4097_SET_TRANSFORM_PROGRAM && count <= 32) return 1;
    if (type == 0 && method >= 0x1a00 && method < 0x1c00 &&
        !(method & 31) && count == 8) return 1;
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
    if (!count) return;
    /* The FIFO admits incrementing packets starting at lane zero here.
     * A missing IO page may still truncate the supplied prefix. */
    if (method == NV4097_SET_TRANSFORM_PROGRAM || (method >= 0x1a00 && method < 0x1c00)) {
        if (rsx) {
            memcpy(rsx->regs + method / 4, words, count * sizeof(u32));
            for (u32 i = 0; i < count; ++i) ++rsx->seen[method / 4 + i];
        }
        if (method == NV4097_SET_TRANSFORM_PROGRAM) {
            if (rsx) {
                u32* load = &rsx->regs[NV4097_SET_TRANSFORM_PROGRAM_LOAD / 4];
                const u32 start = *load * 4u;
                if (start <= RSX_DSP_VP_WORDS - count) {
                    memcpy(rsx->vp + start, words, count * sizeof(u32));
                } else {
                    /* Preserve the guest's wrapping 32-bit instruction address. */
                    for (u32 i = 0; i < count; ++i) {
                        const u32 pos = start + i;
                        if (pos < RSX_DSP_VP_WORDS) rsx->vp[pos] = words[i];
                    }
                }
                *load += count / 4;
            }
            const u32 cursor = state->vp_ucode_write;
            if (cursor < sizeof(state->vp_ucode)) {
                const u32 room = (sizeof(state->vp_ucode) - cursor) / 4;
                const u32 written = count < room ? count : room;
                if (written) {
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                    memcpy(state->vp_ucode + cursor, words, written * 4);
#else
                    for (u32 i = 0; i < written; ++i)
                        for (u32 j = 0; j < 4; ++j)
                            state->vp_ucode[cursor + 4*i + j] = (u8)(words[i] >> (8*j));
#endif
                    state->vp_ucode_write += written * 4;
                    if (state->vp_ucode_write > state->vp_ucode_bytes)
                        state->vp_ucode_bytes = state->vp_ucode_write;
                    state->vp_dirty = 1;
                }
            }
        } else if (count == 8) {
            rsx_texture_state* t = &state->textures[(method - 0x1a00) / 32];
            t->offset = words[0] & 0x7fffffffu;
            t->format = words[1];
            t->address = words[2];
            t->control0 = words[3];
            t->control1 = words[4];
            t->filter = words[5];
            t->image_rect = words[6];
            t->border_color = words[7];
            t->dirty = state->texture_dirty = 1;
        } else {
            for (u32 i = 0; i < count; ++i)
                rsx_process_method(state, method + i * 4, words[i]);
        }
        return;
    }
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
            if(slot<RSX_DSP_NUM_CONSTANTS) {
                rsx->constants[slot][lane]=value;
                /* Same extent bookkeeping as rsx_dispatch_method. */
                if(slot>=g_rsx_dsp_constants_hw)rsx_dsp_constants_note(slot+1u);
            }
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

#pragma once
#include "rsx_dispatch.h"
#include <string.h>

/* One pipeline decision on the serialized RSX thread. Rebuilt for every draw;
 * it holds decoded register state, never guest pixels or resource handles. */
typedef struct {
    rsx_dsp_texture texture[RSX_DSP_NUM_TEXTURES];
    u32 cube, volume, unnormalized;
    u32 dimensions[RSX_DSP_NUM_TEXTURES][2];
} buzz_texture_snapshot;

static inline void buzz_texture_snapshot_init(buzz_texture_snapshot* out,
    const rsx_dispatch* rsx, int volume_supported) {
    out->cube=out->volume=out->unnormalized=0;
    for(u32 unit=0;unit<RSX_DSP_NUM_TEXTURES;++unit) {
        rsx_dsp_texture* t=&out->texture[unit];
        rsx_dsp_get_texture(rsx,unit,t);
        out->dimensions[unit][0]=out->dimensions[unit][1]=0;
        if(!t->enabled)continue;
        if(t->cubemap && t->width && t->width==t->height)
            out->cube|=1u<<unit;
        if(volume_supported && !t->cubemap && t->dimension==3 && t->depth>1 && t->width && t->height)
            out->volume|=1u<<unit;
        if((t->format&RSX_TEX_FMT_UNNORM) && t->width && t->height) {
            out->unnormalized|=1u<<unit;
            out->dimensions[unit][0]=t->width;
            out->dimensions[unit][1]=t->height;
        }
    }
}

static inline u32 buzz_texture_snapshot_dimensions(const buzz_texture_snapshot* snapshot,
    u32 dimensions[RSX_DSP_NUM_TEXTURES][2]) {
    memcpy(dimensions,snapshot->dimensions,sizeof snapshot->dimensions);
    return snapshot->unnormalized;
}

#pragma once
#include "rsx_vertex_compact.h"
#include "rsx_vertex_formats.h"
#include <string.h>

/* Engine-local plan: the layout is immutable until this draw finishes.
 * Prepare/fetch only visit its selected attributes. Inline packing continues
 * to use rsx_vertex_inline_layout(rsx), including every declared attribute. */
static inline void buzz_vertex_plan_init(rsx_vertex_fetch_plan* plan,const rsx_dispatch* rsx,
    const rsx_vertex_layout_plan* layout,rsx_vertex_guest_ptr_fn guest_ptr,void* guest_user)
{
    if(!plan || !rsx || !layout)return;
    /* The SDK's straight 16-attribute loop is faster for wide layouts. */
    if(layout->count>=8) {
        rsx_vertex_fetch_plan_init(plan,rsx,layout,guest_ptr,guest_user);return;
    }
    memset(plan,0,sizeof(*plan));plan->layout=*layout;
    plan->base_offset=rsx_dsp_vertex_data_base_offset(rsx);
    plan->divider_mask=rsx_dsp_reg(rsx,0x1FC0);
    plan->guest_ptr=guest_ptr;plan->guest_user=guest_user;
    for(u32 slot=0;slot<layout->count;++slot) {
        const u32 attr=layout->attrs[slot];
        rsx_vertex_fetch_attr* fetch=&plan->attr[attr];
        rsx_dsp_get_vertex_attr(rsx,attr,&fetch->desc);
        fetch->elem_size=rsx_vertex_attrib_size(fetch->desc.type,fetch->desc.size);
        fetch->stride=fetch->desc.stride?fetch->desc.stride:fetch->elem_size;
        rsx_dsp_vertex_default(rsx,attr,fetch->default_value);
        if(attr==3 && fetch->default_value[0]==0.0f && fetch->default_value[1]==0.0f &&
           fetch->default_value[2]==0.0f && fetch->default_value[3]==1.0f) {
            fetch->default_value[0]=fetch->default_value[1]=fetch->default_value[2]=1.0f;
        }
    }
}

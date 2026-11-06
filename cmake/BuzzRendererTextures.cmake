# Included by the CPU adapter after its optional program transformations.
option(BUZZ_RENDERER_TEXTURE_SNAPSHOT "Decode texture registers once per pipeline decision" OFF)
set(renderer_texture_snapshot 0)
if(BUZZ_RENDERER_TEXTURE_SNAPSHOT)
    set(renderer_texture_snapshot 1)
endif()
string(FIND "${engine_text}" "static u32 eng_pipeline_get(" texture_start)
string(FIND "${engine_text}" "/* ---- draw accumulation " texture_end)
if(texture_start LESS 0 OR texture_end LESS texture_start)
    message(FATAL_ERROR "Texture snapshot adapter no longer matches SDK")
endif()
math(EXPR texture_length "${texture_end}-${texture_start}")
string(SUBSTRING "${engine_text}" 0 ${texture_start} texture_before)
string(SUBSTRING "${engine_text}" ${texture_start} ${texture_length} texture_body)
string(SUBSTRING "${engine_text}" ${texture_end} -1 texture_after)
function(buzz_texture_replace old new)
    string(FIND "${texture_body}" "${old}" location)
    if(location EQUAL -1)
        message(FATAL_ERROR "Texture snapshot adapter no longer matches SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" updated "${texture_body}")
    set(texture_body "${updated}" PARENT_SCOPE)
endfunction()
buzz_texture_replace("    const u32 cube_mask = eng_cube_mask();" [=[
    const int snapshot_enabled=buzz_texture_snapshot_enabled();
    buzz_texture_snapshot texture_snapshot;
    if(snapshot_enabled)buzz_texture_snapshot_init(&texture_snapshot,&g.rsx,
        g.be->texture_create_3d && g.be->texture_upload_3d);
    const u32 cube_mask = snapshot_enabled ? texture_snapshot.cube : eng_cube_mask();
]=])
buzz_texture_replace("eng_volume_mask()" "(snapshot_enabled ? texture_snapshot.volume : eng_volume_mask())")
buzz_texture_replace("eng_unnorm_units(unnorm_dim)" "(snapshot_enabled ? buzz_texture_snapshot_dimensions(&texture_snapshot,unnorm_dim) : eng_unnorm_units(unnorm_dim))")
buzz_texture_replace("rsx_dsp_get_texture(&g.rsx, u, &t);" "if(snapshot_enabled)t=texture_snapshot.texture[u];else rsx_dsp_get_texture(&g.rsx, u, &t);")
set(engine_text "${texture_before}${texture_body}${texture_after}")
string(APPEND renderer_prefix [=[

#include "renderer_texture_snapshot.h"
static int buzz_texture_snapshot_enabled(void) {
    static int enabled=-1;
    if(enabled<0) {
        const char* value=getenv("BUZZ_TEXTURE_SNAPSHOT");
        enabled=value && *value ? !strcmp(value,"1") : @renderer_texture_snapshot@;
        fprintf(stderr,"[rsx engine] texture snapshot=%d\n",enabled);
    }
    return enabled;
}
]=])

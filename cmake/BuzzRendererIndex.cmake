option(BUZZ_RENDERER_INDEX_FUSION "Fuse contiguous u16 index expansion and remapping" OFF)
option(BUZZ_RENDERER_PACKED_FUSION "Use compact private stamps for fused u16 remapping" OFF)
if(BUZZ_RENDERER_PACKED_FUSION)
    set(renderer_prefix "#define BUZZ_RENDERER_PACKED_FUSION 1\n${renderer_prefix}")
endif()
if(BUZZ_RENDERER_INDEX_FUSION)
    set(index_fusion_default 1)
else()
    set(index_fusion_default 0)
endif()
function(buzz_index_replace old new)
    string(FIND "${engine_text}" "${old}" index_found)
    if(index_found EQUAL -1)
        message(FATAL_ERROR "Index fusion adapter no longer matches SDK: ${old}")
    endif()
    string(REPLACE "${old}" "${new}" engine_text "${engine_text}")
    set(engine_text "${engine_text}" PARENT_SCOPE)
endfunction()
buzz_index_replace("static void dc_fetch(const rsx_vertex_layout_plan* layout, int allow_remap)"
"#include \"renderer_index_fusion.h\"
static int buzz_index_fusion_enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char* value=getenv(\"BUZZ_INDEX_FUSION\");enabled=value&&*value?!strcmp(value,\"1\"):${index_fusion_default};
        fprintf(stderr,\"[rsx engine] fused index remap=%d\\n\",enabled);}
    return enabled;
}
static void dc_fetch(const rsx_vertex_layout_plan* layout, int allow_remap)")
buzz_index_replace("    for (u32 b = 0; b < dc.n_arr && dc.fetch_ok; b++)"
"    int buzz_fused=0;
    for (u32 b = 0; b < dc.n_arr && dc.fetch_ok; b++)")
buzz_index_replace("            for (u32 i = 0; i < count && dc.fetch_ok; i++) {"
"            if(run && allow_remap && !restart_en && !ia.is_u32 && !dc.n_arr && dc.n_idx==1 && count>=4 && count<=65536) {
                if(s_eng_perf.on){buzz_index_eligible++;buzz_index_refs+=count;}
                u32 unique;
                if(buzz_index_fusion_enabled() && buzz_index_fuse_u16(&dc.ref_remap,&dc.refs,&dc.cap_refs,run,count,base_index,&unique)) {
                    dc.n_refs=unique;dc.n_source_refs=count;dc.refs_remapped=unique<count;buzz_fused=1;
                    if(s_eng_perf.on)buzz_index_fused++;
                    continue;
                }
            }
            for (u32 i = 0; i < count && dc.fetch_ok; i++) {")
buzz_index_replace("    dc.n_source_refs = dc.n_refs;"
    "    if(!buzz_fused)dc.n_source_refs = dc.n_refs;")
buzz_index_replace("    if (allow_remap && dc.n_refs > 1) {"
    "    if (!buzz_fused && allow_remap && dc.n_refs > 1) {")
if(BUZZ_RENDERER_PACKED_FUSION)
    # Validate each span before emitting anything. A failed translation or an
    # unsupported group retains the ordinary per-index fallback. Packet gaps
    # and overlapping ranges are legal and keep their original occurrence order.
    buzz_index_replace("        for (u32 b = 0; b < dc.n_idx && dc.fetch_ok; b++) {" [=[
        if(allow_remap && !restart_en && !ia.is_u32 && !dc.n_arr && dc.n_idx>1 &&
           buzz_index_fusion_enabled()) {
            static buzz_index_span spans[ENG_MAX_BATCHES];
            u32 total=0;
            int valid=1;
            for(u32 b=0;b<dc.n_idx;++b) {
                const u32 count=dc.idx[b].count;
                const u64 start=(u64)ia.offset+(u64)dc.idx[b].first*2u;
                const u64 bytes=(u64)count*2u;
                if(count>65536 || total>65536-count || start>0xffffffffull ||
                   start+bytes>0x100000000ull) {valid=0;break;}
                spans[b].count=count;
                spans[b].bytes=count?eng_guest_ptr(NULL,ia_loc,(u32)start,(u32)bytes):NULL;
                if(count && !spans[b].bytes){valid=0;break;}
                total+=count;
            }
            u32 unique;
            if(valid && buzz_index_packed_spans(buzz_index_packed_storage(),&dc.ref_remap,&dc.refs,
                &dc.cap_refs,spans,dc.n_idx,base_index,&unique)) {
                dc.n_refs=unique;dc.n_source_refs=total;dc.refs_remapped=unique<total;
                buzz_fused=1;
                if(s_eng_perf.on){buzz_index_eligible++;buzz_index_fused++;buzz_index_refs+=total;}
            }
        }
        for (u32 b = 0; !buzz_fused && b < dc.n_idx && dc.fetch_ok; b++) {
]=])
endif()
# Report alongside existing engine diagnostics; no counters in FPS runs.
buzz_index_replace("static void eng_profile_report(void)"
    "static u64 buzz_index_eligible,buzz_index_fused,buzz_index_refs;\nstatic void eng_profile_report(void)")
buzz_index_replace("    memset(&s_eng_texture_perf,0,sizeof s_eng_texture_perf);\n    s_eng_perf.bulk_lists=s_eng_perf.reference_lists=0;"
"    fprintf(stderr,\"[rsx engine] index-fusion eligible=%llu fused=%llu refs=%llu\\n\",(unsigned long long)buzz_index_eligible,(unsigned long long)buzz_index_fused,(unsigned long long)buzz_index_refs);
    buzz_index_eligible=buzz_index_fused=buzz_index_refs=0;
    memset(&s_eng_texture_perf,0,sizeof s_eng_texture_perf);
    s_eng_perf.bulk_lists=s_eng_perf.reference_lists=0;")

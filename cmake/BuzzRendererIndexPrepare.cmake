option(BUZZ_RENDERER_INDEX_PREPARE "Bulk index references and packed dense remapping" OFF)
if(NOT BUZZ_RENDERER_INDEX_PREPARE)
    return()
endif()
set(packed_anchor "    return enabled?buzz_vertex_remap_build(remap,refs,count,unique):rsx_vertex_remap_build(remap,refs,count,unique);")
string(FIND "${renderer_prefix}" "${packed_anchor}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Packed remap adapter cannot find remap wrapper")
endif()
string(REPLACE "${packed_anchor}" [=[
    static buzz_packed_remap scratch;
    static int packed=-1;
    if(packed<0){const char* p=getenv("BUZZ_PACKED_REMAP");packed=!(p&&!strcmp(p,"0"));
        fprintf(stderr,"[rsx engine] packed index remap=%d\n",packed);}
    if(packed)return buzz_packed_remap_build(&scratch,remap,refs,count,unique);
    return enabled?buzz_vertex_remap_build(remap,refs,count,unique):rsx_vertex_remap_build(remap,refs,count,unique);
]=] renderer_prefix "${renderer_prefix}")
set(renderer_prefix "#include \"renderer_packed_remap.h\"\n${renderer_prefix}")
set(append_anchor "            for (u32 i = 0; i < count && dc.fetch_ok; i++) {")
string(FIND "${engine_text}" "${append_anchor}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Index append adapter cannot find index loop")
endif()
string(REPLACE "${append_anchor}" [=[
            static int append=-1;
            if(append<0){const char* p=getenv("BUZZ_INDEX_APPEND");append=!(p&&!strcmp(p,"0"));
                fprintf(stderr,"[rsx engine] bulk index append=%d\n",append);}
            if(append && run && !restart_en && buzz_index_append(&dc.refs,&dc.cap_refs,&dc.n_refs,
                run,count,base_index,ia.is_u32))continue;
            for (u32 i = 0; i < count && dc.fetch_ok; i++) {
]=] engine_text "${engine_text}")
set(renderer_prefix "#include \"renderer_index_append.h\"\n${renderer_prefix}")

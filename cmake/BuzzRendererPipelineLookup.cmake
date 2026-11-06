option(BUZZ_RENDERER_PIPELINE_LOOKUP "Index the existing RSX pipeline key table" OFF)
if(NOT BUZZ_RENDERER_PIPELINE_LOOKUP)
    return()
endif()
set(lookup_anchor "    for (u32 i = 0; i < g.n_pipelines; i++)\n        if (g.pipelines[i].key == key) {")
set(lookup_function "static u32 eng_pipeline_get(")
foreach(anchor IN ITEMS "${lookup_anchor}" "${lookup_function}")
    string(FIND "${engine_text}" "${anchor}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Pipeline lookup adapter no longer matches SDK: ${anchor}")
    endif()
endforeach()
string(REPLACE "${lookup_function}" "#include \"renderer_pipeline_lookup.h\"\n${lookup_function}" engine_text "${engine_text}")
string(REPLACE "${lookup_anchor}" [=[
    const u32 buzz_pipeline_index=buzz_pipeline_find(key);
    if(buzz_pipeline_index<g.n_pipelines) {
            const u32 i=buzz_pipeline_index;
]=] engine_text "${engine_text}")

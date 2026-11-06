option(BUZZ_RENDERER_VERTEX_STREAM "Convert common vertex layouts four vertices at a time" OFF)
option(BUZZ_RENDERER_NORMAL_MULTIPLY "Exact finite-domain CMP normalization with multiplication" OFF)
if(BUZZ_RENDERER_NORMAL_MULTIPLY AND NOT BUZZ_RENDERER_VERTEX_STREAM)
    message(FATAL_ERROR "Exact CMP multiply requires vertex stream conversion")
endif()
if(NOT BUZZ_RENDERER_VERTEX_STREAM)
    return()
endif()
if(BUZZ_RENDERER_NORMAL_MULTIPLY)
    set(renderer_prefix "#define BUZZ_RENDERER_NORMAL_MULTIPLY 1\n${renderer_prefix}")
endif()
set(stream_anchor "    return enabled?buzz_vertex_fetch_many(plan,refs,count,out):rsx_vertex_fetch_many(plan,refs,count,out);")
string(FIND "${renderer_prefix}" "${stream_anchor}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Vertex stream adapter cannot find fetch wrapper")
endif()
string(REPLACE "#include \"renderer_vertex_fetch.h\"" "#include \"renderer_vertex_stream.h\"" renderer_prefix "${renderer_prefix}")
string(REPLACE "${stream_anchor}" [=[
    static int stream=-1;
    if(stream<0) {
        const char* option=getenv("BUZZ_VERTEX_STREAM");
        stream=!(option && !strcmp(option,"0"));
        fprintf(stderr,"[rsx engine] four-vertex stream=%d\n",stream);
    }
    if(stream)return buzz_vertex_stream(plan,refs,count,out);
    return enabled?buzz_vertex_fetch_many(plan,refs,count,out):rsx_vertex_fetch_many(plan,refs,count,out);
]=] renderer_prefix "${renderer_prefix}")

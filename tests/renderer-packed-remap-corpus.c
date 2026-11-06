#include "renderer_packed_remap.h"
static buzz_packed_remap packed;
#define buzz_vertex_remap_build(r,v,n,u) buzz_packed_remap_build(&packed,r,v,n,u)
#include "renderer-remap-corpus.c"

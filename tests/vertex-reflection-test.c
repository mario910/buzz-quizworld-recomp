#include <windows.h>
#include <initguid.h>
#include "../ps3recomp/libs/video/rsx_d3d12_vertex_reflection.h"
#include <assert.h>
#include <stdio.h>
static uint32_t inspect(const char* source)
{
    ID3DBlob *blob=NULL,*errors=NULL;
    HRESULT hr=D3DCompile(source,strlen(source),"vertex-input-test",NULL,NULL,"main","vs_5_0",0,0,&blob,&errors);
    if(FAILED(hr)){if(errors)puts(errors->lpVtbl->GetBufferPointer(errors));abort();}
    uint32_t mask=rsx_d3d12_vertex_inputs(blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob));
    blob->lpVtbl->Release(blob);if(errors)errors->lpVtbl->Release(errors);return mask;
}
int main(void)
{
    assert(inspect("struct V{float4 p:ATTR0;float4 c:ATTR8;}; float4 main(V i):SV_POSITION{return i.p+i.c;}")==0x101);
    assert(inspect("struct V{float4 p:ATTR15;}; float4 main(V i):SV_POSITION{return i.p;}")==0x8000);
    assert(inspect("float4 main(float4 p:POSITION):SV_POSITION{return p;}")==0xffff);
    assert(inspect("float4 main(uint id:SV_VertexID):SV_POSITION{return float4(id,0,0,1);}")==0xffff);
    puts("PASS: actual compiled ATTR signatures, sparse/high registers, unknown semantics and system-only fallback");
}

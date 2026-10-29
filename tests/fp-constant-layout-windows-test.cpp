#include "../ps3recomp/libs/video/rsx_d3d12_fp_abi.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <cassert>
#include <cstdio>
int main() {
    char source[4096] =
        "cbuffer PSConstants : register(b1) {\nfloat4 fp_constants[1]; float4 fp_alpha;\n};\n"
        "Texture2D rsx_tex[16]:register(t0); SamplerState rsx_samp[16]:register(s0);\n"
        "struct PSInput {float4 pos:SV_POSITION;};\n"
        "float4 main(PSInput input):SV_TARGET {\n"
        "    float4 c=fp_constants[0]+fp_constants[255]+rsx_tex[0].Sample(rsx_samp[0],float2(.2,.3));\n"
        "    return c;\n}\n";
    assert(rsx_d3d12_fp_bind_constants(source,sizeof source,RSX_D3D12_FP_CONSTANTS)==0);
    assert(rsx_d3d12_fp_texture_scales(source,sizeof source,0)==1);
    assert(rsx_d3d12_fp_alpha_test(source,sizeof source)==0);
    ID3DBlob *shader=nullptr,*errors=nullptr;
    HRESULT hr=D3DCompile(source,strlen(source),"test",nullptr,nullptr,"main","ps_5_0",0,0,&shader,&errors);
    if (FAILED(hr)) {if(errors)std::puts((const char*)errors->GetBufferPointer());return 1;}
    ID3D11ShaderReflection* reflect=nullptr;
    assert(SUCCEEDED(D3DReflect(shader->GetBufferPointer(),shader->GetBufferSize(),
                               __uuidof(ID3D11ShaderReflection),(void**)&reflect)));
    auto cb=reflect->GetConstantBufferByName("PSConstants");
    D3D11_SHADER_INPUT_BIND_DESC binding{};
    D3D11_SHADER_DESC shader_description{};
    assert(SUCCEEDED(reflect->GetDesc(&shader_description)));
    for(unsigned i=0;i<shader_description.BoundResources;++i) {
        assert(SUCCEEDED(reflect->GetResourceBindingDesc(i,&binding)));
        std::printf("INFO: resource %s register=%u count=%u type=%u\n",binding.Name,binding.BindPoint,binding.BindCount,binding.Type);
    }
    const char* names[]={"rsx_texscale","rsx_alphatest","fp_constants"};
    const unsigned expected[]={0,64,80};
    for(unsigned i=0;i<3;++i) {
        D3D11_SHADER_VARIABLE_DESC description{};
        assert(SUCCEEDED(cb->GetVariableByName(names[i])->GetDesc(&description)));
        assert(description.StartOffset==expected[i]);
        if(i==2)assert(description.Size==RSX_D3D12_FP_CONSTANTS*16);
    }
    D3D11_SHADER_BUFFER_DESC buffer_description{};
    assert(SUCCEEDED(cb->GetDesc(&buffer_description)));
    assert(buffer_description.Size==(5+RSX_D3D12_FP_CONSTANTS)*16);
    assert(buffer_description.Size<=RSX_D3D12_FP_CB_BYTES);
    reflect->Release();shader->Release();if(errors)errors->Release();
    assert(rsx_d3d12_fp_debug_color(source,sizeof source,1,0,1)==0);
    shader=nullptr;errors=nullptr;
    hr=D3DCompile(source,strlen(source),"forced-color",nullptr,nullptr,"main","ps_5_0",0,0,&shader,&errors);
    if (FAILED(hr)) {if(errors)std::puts((const char*)errors->GetBufferPointer());return 1;}
    assert(SUCCEEDED(D3DReflect(shader->GetBufferPointer(),shader->GetBufferSize(),
                               __uuidof(ID3D11ShaderReflection),(void**)&reflect)));
    assert(SUCCEEDED(reflect->GetDesc(&shader_description)));
    assert(shader_description.BoundResources==0); // no b1/texture/alpha dependencies remain
    reflect->Release();shader->Release();if(errors)errors->Release();
    char mrt[4096] =
        "struct PSOutput {float4 t0:SV_TARGET0;float4 t1:SV_TARGET1;};\n"
        "struct PSInput {float4 pos:SV_POSITION;};\n"
        "PSOutput main(PSInput input) {PSOutput o;o.t0=float4(1,0,0,1);o.t1=float4(0,1,0,1);\n    return o;\n}\n";
    assert(rsx_d3d12_fp_bind_constants(mrt,sizeof mrt,0)==0);
    assert(rsx_d3d12_fp_alpha_test(mrt,sizeof mrt)==0);
    shader=nullptr;errors=nullptr;
    hr=D3DCompile(mrt,strlen(mrt),"mrt-test",nullptr,nullptr,"main","ps_5_0",0,0,&shader,&errors);
    if (FAILED(hr)) {if(errors)std::puts((const char*)errors->GetBufferPointer());return 1;}
    shader->Release();if(errors)errors->Release();
    assert(rsx_d3d12_fp_debug_color(mrt,sizeof mrt,1,0,1)==0);
    shader=nullptr;errors=nullptr;
    hr=D3DCompile(mrt,strlen(mrt),"forced-mrt-color",nullptr,nullptr,"main","ps_5_0",0,0,&shader,&errors);
    if (FAILED(hr)) {if(errors)std::puts((const char*)errors->GetBufferPointer());return 1;}
    shader->Release();if(errors)errors->Release();
    std::puts("PASS: D3DCompile/reflection confirms b1 offsets 0 / 64 / 80 bytes");
    std::puts("PASS: dynamic alpha test compiles for single and multiple render targets");
    std::puts("PASS: early-return diagnostic compiles with no material resources, including MRT");
    return 0;
}

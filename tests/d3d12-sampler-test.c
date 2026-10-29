#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "../ps3recomp/libs/video/rsx_d3d12_sampler.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    for(unsigned mn=1;mn<=6;++mn)for(unsigned mg=1;mg<=2;++mg) {
        RsxSamplerKey k=rsx_sampler_key((mg<<24)|(mn<<16),0x030201,0,0);
        D3D12_SAMPLER_DESC d=rsx_sampler_decode(k);
        assert(D3D12_DECODE_MIN_FILTER(d.Filter)==((mn==2||mn==4||mn==6)?1:0));
        assert(D3D12_DECODE_MAG_FILTER(d.Filter)==(mg==2));
        assert(D3D12_DECODE_MIP_FILTER(d.Filter)==(mn>=5));
        assert(d.AddressU==D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        assert(d.AddressV==D3D12_TEXTURE_ADDRESS_MODE_MIRROR);
        assert(d.AddressW==D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    }
    RsxSamplerKey k=rsx_sampler_key(0x02060000|0x1f00,0x040404,(512u<<19)|(768u<<7),0xff804020);
    D3D12_SAMPLER_DESC d=rsx_sampler_decode(k);
    assert(d.MinLOD==2 && d.MaxLOD==3 && d.MipLODBias==-1);
    assert(d.AddressW==D3D12_TEXTURE_ADDRESS_MODE_BORDER && d.BorderColor[3]==1);
    assert(d.BorderColor[0]==128.0f/255 && d.BorderColor[2]==32.0f/255);
    RsxSamplerKey other=rsx_sampler_key(k.filter,k.address,k.control0|0x80000001u,k.border);
    assert(!memcmp(&k,&other,sizeof k));
    other=rsx_sampler_key(k.filter,k.address^0x10000,k.control0,k.border);
    assert(memcmp(&k,&other,sizeof k));
    other=rsx_sampler_key(k.filter,k.address,k.control0^(1u<<30),k.border);
    assert(memcmp(&k,&other,sizeof k));
    d=rsx_sampler_decode(rsx_sampler_key(0x02020000,0,1024u<<19,0));
    assert(d.MaxLOD==d.MinLOD);
    puts("PASS: guest min/mag/mip, U/V/W, LOD, signed bias, border and cache key");
}

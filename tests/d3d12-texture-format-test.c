#include "../ps3recomp/libs/video/rsx_d3d12_texture_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const struct { unsigned guest; DXGI_FORMAT host; } cases[] = {
        {0x81,DXGI_FORMAT_R8_UNORM},{0x82,DXGI_FORMAT_R8G8B8A8_UNORM},
        {0x83,DXGI_FORMAT_R8G8B8A8_UNORM},{0x84,DXGI_FORMAT_R8G8B8A8_UNORM},
        {0x85,DXGI_FORMAT_R8G8B8A8_UNORM},{0x86,DXGI_FORMAT_BC1_UNORM},
        {0x87,DXGI_FORMAT_BC2_UNORM},{0x88,DXGI_FORMAT_BC3_UNORM},
        {0x8b,DXGI_FORMAT_R8G8_UNORM},{0x8c,DXGI_FORMAT_R8G8_UNORM},
        {0x8d,DXGI_FORMAT_R8G8_UNORM},{0x90,DXGI_FORMAT_R32_FLOAT},
        {0x92,DXGI_FORMAT_R16_UNORM},{0x94,DXGI_FORMAT_R16_UNORM},
        {0x95,DXGI_FORMAT_R16G16_UNORM},{0x97,DXGI_FORMAT_R8G8B8A8_UNORM},
        {0x9a,DXGI_FORMAT_R16G16B16A16_FLOAT},{0x9b,DXGI_FORMAT_R32G32B32A32_FLOAT},
        {0x9c,DXGI_FORMAT_R32_FLOAT},{0x9d,DXGI_FORMAT_R8G8B8A8_UNORM},
        {0x9e,DXGI_FORMAT_R8G8B8A8_UNORM},{0x9f,DXGI_FORMAT_R16G16_FLOAT}
    };
    for (unsigned n=0;n<sizeof cases/sizeof *cases;++n) for (unsigned flags=0;flags<4;++flags) {
        unsigned format=cases[n].guest | (flags << 5);
        rsx_tex_layout layout;
        assert(rsx_d3d12_texture_supported(format));
        rsx_texture_layout(format,16,16,&layout);
        assert(rsx_d3d12_texture_format(layout.fmt)==cases[n].host);
    }
    assert(!rsx_d3d12_texture_supported(0x89));
    assert(!rsx_d3d12_texture_supported(0xff & ~0x80));
    assert(rsx_d3d12_texture_format((rsx_texfmt)100)==DXGI_FORMAT_UNKNOWN);
    /* Padded linear rows must not consume the padding as the next row. */
    rsx_tex_layout layout;
    rsx_texture_layout_pitched(0xa3,1,2,4,&layout);
    const unsigned char source[]={0xff,0x00,0x12,0x34,0xf0,0xf0,0x56,0x78};
    unsigned char decoded[8]={0};
    rsx_texture_decode(decoded,4,source,1,2,&layout,0);
    const unsigned char wanted[]={255,0,0,255,0,255,0,255};
    assert(!memcmp(decoded,wanted,8));
    /* BC2/3 faces use 16-byte blocks, not BC1's eight-byte block stride. */
    assert(rsx_texture_cube_face_stride(0x86,16,16,1,0)==128);
    assert(rsx_texture_cube_face_stride(0x87,16,16,1,0)==256);
    assert(rsx_texture_cube_face_stride(0x88,16,16,1,0)==256);
    puts("D3D12 textures: 88 format/flag mappings, resource/SRV identity, padded rows and BC cube stride PASS");
}

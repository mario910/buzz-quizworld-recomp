/* Keep the existing single-sample/quadrant/footprint checks and add real
 * sample-frequency depth, distinct per-sample stencil, and compute packing. */
#define main zeta_single_sample_test
#include "zeta-gpu-test.c"
#undef main
#include "../ps3recomp/libs/video/rsx_d3d12_msaa_shader.h"

static int msaa_case(int warp,unsigned samples,unsigned grid_y)
{
    IDXGIFactory4*factory=NULL;IDXGIAdapter*adapter=NULL;ID3D12Device*d=NULL;
    OK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    if(warp)OK(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&adapter));
    OK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d));
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality={0};
    quality.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;quality.SampleCount=samples;
    OK(d->lpVtbl->CheckFeatureSupport(d,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof quality));
    if(!quality.NumQualityLevels){fprintf(stderr,"Unsupported %ux depth MSAA\n",samples);return 1;}
    ID3D12CommandQueue*q=NULL;D3D12_COMMAND_QUEUE_DESC qd={0};
    OK(d->lpVtbl->CreateCommandQueue(d,&qd,&IID_ID3D12CommandQueue,(void**)&q));
    ID3D12CommandAllocator*a=NULL;ID3D12GraphicsCommandList*c=NULL;
    OK(d->lpVtbl->CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&a));
    OK(d->lpVtbl->CreateCommandList(d,0,D3D12_COMMAND_LIST_TYPE_DIRECT,a,NULL,&IID_ID3D12GraphicsCommandList,(void**)&c));
    ID3D12RootSignature*pack_root=NULL,*color_root=NULL,*draw_root=NULL;ID3D12PipelineState*pack=NULL,*color_pack=NULL,*draw[4]={0};
    assert(rsx_zeta_compile_pack_samples(d,&pack_root,&pack,samples));
    assert(rsx_msaa_compile_color_pack(d,&color_root,&color_pack,samples));
    ID3DBlob*blob=NULL,*errors=NULL,*vs=NULL,*ps=NULL;
    D3D12_ROOT_SIGNATURE_DESC rd={0};
    OK(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));DROP(errors);errors=NULL;
    OK(d->lpVtbl->CreateRootSignature(d,0,blob->lpVtbl->GetBufferPointer(blob),blob->lpVtbl->GetBufferSize(blob),&IID_ID3D12RootSignature,(void**)&draw_root));DROP(blob);
    static const char shader[]=
        "float4 vs(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}"
        "struct O{float4 color:SV_Target0;float depth:SV_Depth;};"
        "O ps(float4 pos:SV_Position,uint sample:SV_SampleIndex){O o;o.depth=(float(sample)+1.0)/8.0;"
        "uint2 xy=uint2(pos.xy);o.color=float4((17+37*sample+xy.x)/255.0,(201-29*sample-xy.y)/255.0,"
        "(sample^xy.x^xy.y)&1,1);return o;}";
    OK(D3DCompile(shader,sizeof shader-1,"sample_writer",NULL,NULL,"vs","vs_5_0",0,0,&vs,&errors));DROP(errors);errors=NULL;
    HRESULT ps_result=D3DCompile(shader,sizeof shader-1,"sample_writer",NULL,NULL,"ps","ps_5_0",0,0,&ps,&errors);
    if(FAILED(ps_result) && errors)fprintf(stderr,"%s\n",(const char*)errors->lpVtbl->GetBufferPointer(errors));
    OK(ps_result);DROP(errors);errors=NULL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gp={0};gp.pRootSignature=draw_root;
    gp.VS.pShaderBytecode=vs->lpVtbl->GetBufferPointer(vs);gp.VS.BytecodeLength=vs->lpVtbl->GetBufferSize(vs);
    gp.PS.pShaderBytecode=ps->lpVtbl->GetBufferPointer(ps);gp.PS.BytecodeLength=ps->lpVtbl->GetBufferSize(ps);
    gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    gp.RasterizerState.DepthClipEnable=TRUE;gp.RasterizerState.MultisampleEnable=TRUE;
    gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gp.DepthStencilState.DepthEnable=TRUE;gp.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    gp.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;gp.DepthStencilState.StencilEnable=TRUE;
    gp.DepthStencilState.StencilReadMask=gp.DepthStencilState.StencilWriteMask=255;
    gp.DepthStencilState.FrontFace.StencilFailOp=gp.DepthStencilState.FrontFace.StencilDepthFailOp=D3D12_STENCIL_OP_KEEP;
    gp.DepthStencilState.FrontFace.StencilPassOp=D3D12_STENCIL_OP_REPLACE;gp.DepthStencilState.FrontFace.StencilFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    gp.DepthStencilState.BackFace=gp.DepthStencilState.FrontFace;
    gp.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;gp.SampleDesc.Count=samples;
    gp.NumRenderTargets=1;gp.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    for(unsigned n=0;n<8;++n){D3D12_RENDER_TARGET_BLEND_DESC*b=&gp.BlendState.RenderTarget[n];
        b->SrcBlend=b->SrcBlendAlpha=D3D12_BLEND_ONE;b->DestBlend=b->DestBlendAlpha=D3D12_BLEND_ZERO;
        b->BlendOp=b->BlendOpAlpha=D3D12_BLEND_OP_ADD;b->LogicOp=D3D12_LOGIC_OP_NOOP;b->RenderTargetWriteMask=15;}
    for(unsigned n=0;n<samples;++n){gp.SampleMask=1u<<n;OK(d->lpVtbl->CreateGraphicsPipelineState(d,&gp,&IID_ID3D12PipelineState,(void**)&draw[n]));}
    DROP(vs);DROP(ps);
    ID3D12DescriptorHeap*heap=NULL,*dsheap=NULL,*rtheap=NULL;D3D12_DESCRIPTOR_HEAP_DESC hd={0};
    hd.NumDescriptors=5;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    OK(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&heap));
    hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.Flags=0;
    OK(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&dsheap));
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;OK(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&rtheap));
    UINT inc=d->lpVtbl->GetDescriptorHandleIncrementSize(d,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu,ds,rt;D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap,&cpu);heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap,&gpu);
    dsheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dsheap,&ds);
    rtheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtheap,&rt);
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=17;td.Height=19;
    td.DepthOrArraySize=td.MipLevels=1;td.SampleDesc.Count=samples;td.Format=DXGI_FORMAT_R24G8_TYPELESS;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv={0};cv.Format=gp.DSVFormat;cv.DepthStencil.Depth=1;
    ID3D12Resource*z=NULL;OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,&IID_ID3D12Resource,(void**)&z));
    D3D12_DEPTH_STENCIL_VIEW_DESC dv={0};dv.Format=gp.DSVFormat;dv.ViewDimension=samples==1?D3D12_DSV_DIMENSION_TEXTURE2D:D3D12_DSV_DIMENSION_TEXTURE2DMS;
    d->lpVtbl->CreateDepthStencilView(d,z,&dv,ds);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv={0};sv.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sv.ViewDimension=samples==1?D3D12_SRV_DIMENSION_TEXTURE2D:D3D12_SRV_DIMENSION_TEXTURE2DMS;
    if(samples==1)sv.Texture2D.MipLevels=1;
    sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d->lpVtbl->CreateShaderResourceView(d,z,&sv,cpu);cpu.ptr+=inc;
    sv.Format=DXGI_FORMAT_X24_TYPELESS_G8_UINT;if(samples==1)sv.Texture2D.PlaneSlice=1;d->lpVtbl->CreateShaderResourceView(d,z,&sv,cpu);cpu.ptr+=inc;
    ID3D12Resource*color=NULL;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&color));
    d->lpVtbl->CreateRenderTargetView(d,color,NULL,rt);
    c->lpVtbl->ClearDepthStencilView(c,ds,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,NULL);
    c->lpVtbl->OMSetRenderTargets(c,1,&rt,FALSE,&ds);c->lpVtbl->SetGraphicsRootSignature(c,draw_root);
    D3D12_VIEWPORT vp={0,0,17,19,0,1};D3D12_RECT scissor={0,0,17,19};
    c->lpVtbl->RSSetViewports(c,1,&vp);c->lpVtbl->RSSetScissorRects(c,1,&scissor);c->lpVtbl->IASetPrimitiveTopology(c,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for(unsigned n=0;n<samples;++n){c->lpVtbl->SetPipelineState(c,draw[n]);c->lpVtbl->OMSetStencilRef(c,17+37*n);c->lpVtbl->DrawInstanced(c,3,1,0,0);}
    c->lpVtbl->OMSetRenderTargets(c,0,NULL,FALSE,NULL);
    barrier(c,z,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const unsigned sx=2,sy=grid_y,width=17*sx,height=19*sy;
    ID3D12Resource*image=NULL,*readback=NULL,*color_image=NULL,*color_readback=NULL;
    td.Width=width;td.Height=height;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,NULL,&IID_ID3D12Resource,(void**)&image));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv={0};uv.Format=td.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    d->lpVtbl->CreateUnorderedAccessView(d,image,NULL,&uv,cpu);
    cpu.ptr+=inc;sv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;if(samples==1)sv.Texture2D.PlaneSlice=0;d->lpVtbl->CreateShaderResourceView(d,color,&sv,cpu);cpu.ptr+=inc;
    OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,NULL,&IID_ID3D12Resource,(void**)&color_image));
    d->lpVtbl->CreateUnorderedAccessView(d,color_image,NULL,&uv,cpu);
    c->lpVtbl->SetDescriptorHeaps(c,1,&heap);c->lpVtbl->SetComputeRootSignature(c,pack_root);c->lpVtbl->SetPipelineState(c,pack);
    c->lpVtbl->SetComputeRootDescriptorTable(c,0,gpu);gpu.ptr+=2*inc;c->lpVtbl->SetComputeRootDescriptorTable(c,1,gpu);
    unsigned values[]={sx,sy,width,height};c->lpVtbl->SetComputeRoot32BitConstants(c,2,4,values,0);c->lpVtbl->Dispatch(c,(width+15)/16,(height+15)/16,1);
    barrier(c,image,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_HEAP_PROPERTIES rb={0};rb.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd={0};
    bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=256*height;bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    OK(d->lpVtbl->CreateCommittedResource(d,&rb,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&readback));
    D3D12_TEXTURE_COPY_LOCATION to={0},from={0};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint.Format=td.Format;to.PlacedFootprint.Footprint.Width=width;to.PlacedFootprint.Footprint.Height=height;
    to.PlacedFootprint.Footprint.Depth=1;to.PlacedFootprint.Footprint.RowPitch=256;from.pResource=image;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    c->lpVtbl->CopyTextureRegion(c,&to,0,0,0,&from,NULL);
    barrier(c,color,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    c->lpVtbl->SetComputeRootSignature(c,color_root);c->lpVtbl->SetPipelineState(c,color_pack);
    D3D12_GPU_DESCRIPTOR_HANDLE colors=gpu;colors.ptr+=inc;c->lpVtbl->SetComputeRootDescriptorTable(c,0,colors);
    colors.ptr+=inc;c->lpVtbl->SetComputeRootDescriptorTable(c,1,colors);
    c->lpVtbl->SetComputeRoot32BitConstants(c,2,4,values,0);c->lpVtbl->Dispatch(c,(width+15)/16,(height+15)/16,1);
    barrier(c,color_image,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    OK(d->lpVtbl->CreateCommittedResource(d,&rb,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&color_readback));
    to.pResource=color_readback;from.pResource=color_image;c->lpVtbl->CopyTextureRegion(c,&to,0,0,0,&from,NULL);OK(c->lpVtbl->Close(c));
    ID3D12CommandList*list=(ID3D12CommandList*)c;q->lpVtbl->ExecuteCommandLists(q,1,&list);
    ID3D12Fence*f=NULL;OK(d->lpVtbl->CreateFence(d,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&f));HANDLE event=CreateEvent(NULL,FALSE,FALSE,NULL);assert(event);
    OK(q->lpVtbl->Signal(q,f,1));OK(f->lpVtbl->SetEventOnCompletion(f,1,event));assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
    void*data=NULL;D3D12_RANGE range={0,256*height};OK(readback->lpVtbl->Map(readback,0,&range,&data));
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        unsigned sample=samples==1?0:(x%sx)+(y%sy)*sx;unsigned char*b=(unsigned char*)data+y*256+x*4;
        unsigned got=((unsigned)b[3]<<16)|((unsigned)b[0]<<8)|b[1],expected=(unsigned)roundf(((sample+1)/8.0f)*16777215.0f);
        int error=(int)got-(int)expected;
        if(error < -1 || error > 1 || b[2]!=17+37*sample){fprintf(stderr,"MSAA %u mismatch at %u,%u sample=%u z=%06X/%06X stencil=%u/%u\n",samples,x,y,sample,got,expected,b[2],17+37*sample);return 1;}
    }
    D3D12_RANGE none={0,0};readback->lpVtbl->Unmap(readback,0,&none);
    OK(color_readback->lpVtbl->Map(color_readback,0,&range,&data));
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        unsigned sample=samples==1?0:(x%sx)+(y%sy)*sx;unsigned char*b=(unsigned char*)data+y*256+x*4;
        const unsigned expected[]={17+37*sample+x/sx,201-29*sample-y/sy,((sample^(x/sx)^(y/sy))&1)*255,255};
        for(unsigned channel=0;channel<4;++channel)if(b[channel]!=expected[channel]){
            fprintf(stderr,"MSAA %u color mismatch at %u,%u sample=%u channel=%u got=%u expected=%u\n",samples,x,y,sample,channel,b[channel],expected[channel]);return 1;
        }
    }
    color_readback->lpVtbl->Unmap(color_readback,0,&none);
    ID3D12InfoQueue*info=NULL;
    if(SUCCEEDED(d->lpVtbl->QueryInterface(d,&IID_ID3D12InfoQueue,(void**)&info))){
        for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);++i){SIZE_T n=0;info->lpVtbl->GetMessage(info,i,NULL,&n);D3D12_MESSAGE*m=malloc(n);
            OK(info->lpVtbl->GetMessage(info,i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"MSAA validation: %s\n",m->pDescription);return 1;}free(m);}DROP(info);
    }
    printf("PASS: %ux raster, %ux%u guest grid, spatial + per-sample color/depth/stencil packing without averaging\n",samples,sx,sy);
    for(unsigned n=0;n<samples;++n)DROP(draw[n]);DROP(color_readback);DROP(color_image);DROP(color);DROP(color_pack);DROP(color_root);DROP(rtheap);
    DROP(readback);DROP(image);DROP(z);DROP(f);DROP(dsheap);DROP(heap);DROP(pack);DROP(pack_root);DROP(draw_root);DROP(c);DROP(a);DROP(q);DROP(d);DROP(adapter);DROP(factory);return 0;
}
int main(int argc,char**argv)
{
    int result=zeta_single_sample_test(argc,argv);if(result)return result;
    if(msaa_case(argc>1,1,1))return 1;
    if(msaa_case(argc>1,1,2))return 1;
    if(msaa_case(argc>1,2,1))return 1;
    return msaa_case(argc>1,4,2);
}

/* Real IA binding/readback parity, not just CPU packing. --warp uses WARP. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../ps3recomp/libs/video/rsx_vertex_upload.h"
#include "../ps3recomp/libs/video/rsx_d3d12_vertex_reflection.h"
#define OK(call) do { HRESULT hr=(call); if(FAILED(hr)){fprintf(stderr,"%s: %08lX\n",#call,hr);return 1;} } while(0)
#define DROP(p) do { if(p)(p)->lpVtbl->Release(p); } while(0)
static ID3DBlob* compile(const char* text,const char* profile)
{
    ID3DBlob *blob=NULL,*error=NULL;
    HRESULT hr=D3DCompile(text,strlen(text),"vertex-upload-parity",NULL,NULL,"main",profile,0,0,&blob,&error);
    if(FAILED(hr)){if(error)fputs(error->lpVtbl->GetBufferPointer(error),stderr);abort();}
    DROP(error);return blob;
}
int main(int argc,char**argv)
{
    (void)argv;
    ID3D12Debug* debug=NULL;
    if(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);DROP(debug);}
    IDXGIFactory4* factory=NULL;IDXGIAdapter* adapter=NULL;ID3D12Device* d=NULL;
    OK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    if(argc>1)OK(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&adapter));
    OK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d));
    ID3D12CommandQueue* q=NULL;D3D12_COMMAND_QUEUE_DESC qd={0};
    OK(d->lpVtbl->CreateCommandQueue(d,&qd,&IID_ID3D12CommandQueue,(void**)&q));
    ID3D12CommandAllocator* a=NULL;ID3D12GraphicsCommandList* c=NULL;
    OK(d->lpVtbl->CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&a));
    OK(d->lpVtbl->CreateCommandList(d,0,D3D12_COMMAND_LIST_TYPE_DIRECT,a,NULL,&IID_ID3D12GraphicsCommandList,(void**)&c));
    ID3DBlob* vs=compile("struct V{float4 p:ATTR0;float4 c:ATTR8;};struct O{float4 p:SV_POSITION;float4 c:COLOR0;};O main(V i){O o;o.p=i.p;o.c=i.c;return o;}","vs_5_0");
    ID3DBlob* ps=compile("float4 main(float4 p:SV_POSITION,float4 c:COLOR0):SV_TARGET{return c;}","ps_5_0");
    uint32_t mask=rsx_d3d12_vertex_inputs(vs->lpVtbl->GetBufferPointer(vs),vs->lpVtbl->GetBufferSize(vs));assert(mask==0x101);
    D3D12_ROOT_SIGNATURE_DESC rd={0};rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rootblob=NULL;ID3D12RootSignature* root=NULL;
    OK(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&rootblob,NULL));
    OK(d->lpVtbl->CreateRootSignature(d,0,rootblob->lpVtbl->GetBufferPointer(rootblob),rootblob->lpVtbl->GetBufferSize(rootblob),&IID_ID3D12RootSignature,(void**)&root));
    DROP(rootblob);
    ID3D12PipelineState* pipelines[2]={0};
    for(unsigned compact=0;compact<2;++compact){
        D3D12_INPUT_ELEMENT_DESC layout[16]={0};
        for(unsigned i=0;i<16;++i){layout[i].SemanticName="ATTR";layout[i].SemanticIndex=i;
            layout[i].Format=DXGI_FORMAT_R32G32B32A32_FLOAT;layout[i].InputSlot=compact?i:0;layout[i].AlignedByteOffset=compact?0:i*16;}
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p={0};p.pRootSignature=root;
        p.VS.pShaderBytecode=vs->lpVtbl->GetBufferPointer(vs);p.VS.BytecodeLength=vs->lpVtbl->GetBufferSize(vs);
        p.PS.pShaderBytecode=ps->lpVtbl->GetBufferPointer(ps);p.PS.BytecodeLength=ps->lpVtbl->GetBufferSize(ps);
        p.InputLayout.pInputElementDescs=layout;p.InputLayout.NumElements=16;p.SampleMask=UINT_MAX;
        p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
        p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;
        p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.SampleDesc.Count=1;
        OK(d->lpVtbl->CreateGraphicsPipelineState(d,&p,&IID_ID3D12PipelineState,(void**)&pipelines[compact]));
    }
    float source[3][16][4]={0};
    const float positions[3][4]={{-1,-1,0,1},{0,1,0,1},{1,-1,0,1}};
    for(unsigned v=0;v<3;++v){memcpy(source[v][0],positions[v],16);source[v][8][v]=1;source[v][8][3]=1;}
    ID3D12Resource* upload=NULL;D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=4096;bd.Height=1;
    bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,NULL,&IID_ID3D12Resource,(void**)&upload));
    unsigned char* mapped=NULL;D3D12_RANGE none={0,0};OK(upload->lpVtbl->Map(upload,0,&none,(void**)&mapped));
    RsxVertexUpload packed[3];uint32_t cursor=16; /* Exercise nonzero offsets and mixed layouts in one buffer. */
    for(unsigned i=0;i<3;++i)assert(rsx_vertex_upload_pack(mapped,4096,&cursor,(const uint8_t*)source,sizeof source,3,i==1?mask:0,&packed[i]));
    D3D12_RANGE written={0,cursor};upload->lpVtbl->Unmap(upload,0,&written);
    ID3D12DescriptorHeap* heap=NULL;D3D12_DESCRIPTOR_HEAP_DESC hd={0};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=3;
    OK(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&heap));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap,&rtv);
    UINT inc=d->lpVtbl->GetDescriptorHandleIncrementSize(d,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource* images[3]={0},*readbacks[3]={0};
    c->lpVtbl->SetGraphicsRootSignature(c,root);c->lpVtbl->IASetPrimitiveTopology(c,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT viewport={0,0,32,32,0,1};D3D12_RECT rect={0,0,32,32};
    c->lpVtbl->RSSetViewports(c,1,&viewport);c->lpVtbl->RSSetScissorRects(c,1,&rect);
    for(unsigned i=0;i<3;++i){
        hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width=32;td.Height=32;td.DepthOrArraySize=1;td.MipLevels=1;td.SampleDesc.Count=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&images[i]));
        d->lpVtbl->CreateRenderTargetView(d,images[i],NULL,rtv);
        c->lpVtbl->OMSetRenderTargets(c,1,&rtv,TRUE,NULL);const float black[4]={0,0,0,0};c->lpVtbl->ClearRenderTargetView(c,rtv,black,0,NULL);rtv.ptr+=inc;
        c->lpVtbl->SetPipelineState(c,pipelines[i==1]);
        D3D12_VERTEX_BUFFER_VIEW views[16]={0};D3D12_GPU_VIRTUAL_ADDRESS base=upload->lpVtbl->GetGPUVirtualAddress(upload);
        if(i==1){for(unsigned attr=0;attr<16;++attr)if(mask&(1u<<attr)){
            views[attr].BufferLocation=base+rsx_vertex_upload_stream(&packed[i],3,attr);views[attr].SizeInBytes=3*16;views[attr].StrideInBytes=16;}
            c->lpVtbl->IASetVertexBuffers(c,0,16,views);
        }else{views[0].BufferLocation=base+packed[i].offset;views[0].SizeInBytes=packed[i].bytes;views[0].StrideInBytes=256;c->lpVtbl->IASetVertexBuffers(c,0,1,views);}
        c->lpVtbl->DrawInstanced(c,3,1,0,0);
        D3D12_RESOURCE_BARRIER b={0};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=images[i];
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        c->lpVtbl->ResourceBarrier(c,1,&b);
        hp.Type=D3D12_HEAP_TYPE_READBACK;bd.Width=256*32;
        OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&readbacks[i]));
        D3D12_TEXTURE_COPY_LOCATION to={0},from={0};to.pResource=readbacks[i];to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Footprint.Format=td.Format;to.PlacedFootprint.Footprint.Width=32;to.PlacedFootprint.Footprint.Height=32;
        to.PlacedFootprint.Footprint.Depth=1;to.PlacedFootprint.Footprint.RowPitch=256;
        from.pResource=images[i];from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->lpVtbl->CopyTextureRegion(c,&to,0,0,0,&from,NULL);
    }
    OK(c->lpVtbl->Close(c));ID3D12CommandList* list=(ID3D12CommandList*)c;q->lpVtbl->ExecuteCommandLists(q,1,&list);
    ID3D12Fence* fence=NULL;OK(d->lpVtbl->CreateFence(d,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&fence));
    HANDLE event=CreateEvent(NULL,FALSE,FALSE,NULL);assert(event);OK(q->lpVtbl->Signal(q,fence,1));OK(fence->lpVtbl->SetEventOnCompletion(fence,1,event));
    assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
    void* pixels[3]={0};D3D12_RANGE range={0,256*32};
    for(unsigned i=0;i<3;++i)OK(readbacks[i]->lpVtbl->Map(readbacks[i],0,&range,&pixels[i]));
    unsigned covered=0;
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){unsigned offset=y*256+x*4;
        assert(!memcmp((char*)pixels[0]+offset,(char*)pixels[1]+offset,4));assert(!memcmp((char*)pixels[0]+offset,(char*)pixels[2]+offset,4));
        unsigned char* p=(unsigned char*)pixels[0]+offset;if(p[3]){++covered;assert(p[3]==255);assert(p[0]+p[1]+p[2]>=253 && p[0]+p[1]+p[2]<=257);}}
    assert(covered>400 && covered<600);
    for(unsigned i=0;i<3;++i)readbacks[i]->lpVtbl->Unmap(readbacks[i],0,&none);
    ID3D12InfoQueue* info=NULL;
    if(SUCCEEDED(d->lpVtbl->QueryInterface(d,&IID_ID3D12InfoQueue,(void**)&info))){
        for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);++i){SIZE_T n=0;info->lpVtbl->GetMessage(info,i,NULL,&n);D3D12_MESSAGE* m=malloc(n);
            OK(info->lpVtbl->GetMessage(info,i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"GPU validation: %s\n",m->pDescription);return 1;}free(m);}DROP(info);
    }
    printf("PASS: %s GPU pixel-exact AoS/SoA/fallback, sparse ATTR0+8, 16 input slots, %u covered pixels\n",argc>1?"WARP":"hardware",covered);
    for(unsigned i=0;i<3;++i){DROP(readbacks[i]);DROP(images[i]);}
    DROP(fence);DROP(heap);DROP(upload);DROP(pipelines[0]);DROP(pipelines[1]);DROP(vs);DROP(ps);DROP(root);DROP(c);DROP(a);DROP(q);DROP(d);DROP(adapter);DROP(factory);return 0;
}

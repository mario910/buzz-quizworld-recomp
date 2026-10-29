/* Exercise the production conversion shader on D3D12, with exact stencil and
 * independent D24 comparisons. Uses hardware by default, WARP with --warp. */
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "../ps3recomp/libs/video/rsx_d3d12_zeta_shader.h"
#define OK(call) do{HRESULT hr=(call);if(FAILED(hr)){fprintf(stderr,"%s: %08lX\n",#call,hr);return 1;}}while(0)
#define DROP(p) do{if(p)(p)->lpVtbl->Release(p);}while(0)
static void barrier(ID3D12GraphicsCommandList*c,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER t={0};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition.pResource=r;t.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    t.Transition.StateBefore=a;t.Transition.StateAfter=b;c->lpVtbl->ResourceBarrier(c,1,&t);
}
int main(int argc,char**argv){
    ID3D12Debug*debug=NULL;if(SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug))){debug->lpVtbl->EnableDebugLayer(debug);DROP(debug);}
    IDXGIFactory4*factory=NULL;IDXGIAdapter*adapter=NULL;ID3D12Device*d=NULL;
    OK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    if(argc>1)OK(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&adapter));
    OK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&d));
    ID3D12CommandQueue*q=NULL;D3D12_COMMAND_QUEUE_DESC qd={0};
    OK(d->lpVtbl->CreateCommandQueue(d,&qd,&IID_ID3D12CommandQueue,(void**)&q));
    ID3D12CommandAllocator*a=NULL;ID3D12GraphicsCommandList*c=NULL;
    OK(d->lpVtbl->CreateCommandAllocator(d,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&a));
    OK(d->lpVtbl->CreateCommandList(d,0,D3D12_COMMAND_LIST_TYPE_DIRECT,a,NULL,&IID_ID3D12GraphicsCommandList,(void**)&c));
    ID3D12RootSignature*root=NULL;ID3D12PipelineState*pso=NULL;assert(rsx_zeta_compile_pack(d,&root,&pso));
    ID3D12DescriptorHeap*heap=NULL,*dsheap=NULL;D3D12_DESCRIPTOR_HEAP_DESC hd={0};
    hd.NumDescriptors=5;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    OK(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&heap));
    hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.Flags=0;
    OK(d->lpVtbl->CreateDescriptorHeap(d,&hd,&IID_ID3D12DescriptorHeap,(void**)&dsheap));
    UINT inc=d->lpVtbl->GetDescriptorHandleIncrementSize(d,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu,ds;D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(heap,&cpu);heap->lpVtbl->GetGPUDescriptorHandleForHeapStart(heap,&gpu);
    dsheap->lpVtbl->GetCPUDescriptorHandleForHeapStart(dsheap,&ds);
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td={0};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=17;td.Height=19;
    td.DepthOrArraySize=1;td.MipLevels=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R24G8_TYPELESS;
    td.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv={0};cv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;cv.DepthStencil.Depth=1;
    ID3D12Resource*z=NULL;OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_DEPTH_WRITE,&cv,&IID_ID3D12Resource,(void**)&z));
    D3D12_DEPTH_STENCIL_VIEW_DESC dv={0};dv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
    d->lpVtbl->CreateDepthStencilView(d,z,&dv,ds);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv={0};sv.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sv.Texture2D.MipLevels=1;
    d->lpVtbl->CreateShaderResourceView(d,z,&sv,cpu);cpu.ptr+=inc;
    sv.Format=DXGI_FORMAT_X24_TYPELESS_G8_UINT;sv.Texture2D.PlaneSlice=1;d->lpVtbl->CreateShaderResourceView(d,z,&sv,cpu);cpu.ptr+=inc;
    const float depths[4]={0.125f,0.333f,0.75f,1.0f};const unsigned char stencils[4]={17,63,128,255};
    D3D12_RECT rectangles[4]={{0,0,8,9},{8,0,17,9},{0,9,8,19},{8,9,17,19}};
    for(unsigned i=0;i<4;++i)c->lpVtbl->ClearDepthStencilView(c,ds,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,depths[i],stencils[i],1,&rectangles[i]);
    barrier(c,z,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    c->lpVtbl->SetDescriptorHeaps(c,1,&heap);c->lpVtbl->SetComputeRootSignature(c,root);c->lpVtbl->SetPipelineState(c,pso);
    c->lpVtbl->SetComputeRootDescriptorTable(c,0,gpu);
    ID3D12Resource*images[3]={0},*readbacks[3]={0};unsigned sx[3]={1,2,2},sy[3]={1,1,2};
    for(unsigned i=0;i<3;++i){
        td.Width=17*sx[i];td.Height=19*sy[i];td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        OK(d->lpVtbl->CreateCommittedResource(d,&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,NULL,&IID_ID3D12Resource,(void**)&images[i]));
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv={0};uv.Format=td.Format;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        d->lpVtbl->CreateUnorderedAccessView(d,images[i],NULL,&uv,cpu);cpu.ptr+=inc;
        D3D12_GPU_DESCRIPTOR_HANDLE uav=gpu;uav.ptr+=(UINT64)(2+i)*inc;c->lpVtbl->SetComputeRootDescriptorTable(c,1,uav);
        unsigned values[4]={sx[i],sy[i],17*sx[i],19*sy[i]};c->lpVtbl->SetComputeRoot32BitConstants(c,2,4,values,0);
        c->lpVtbl->Dispatch(c,(values[2]+15)/16,(values[3]+15)/16,1);
        barrier(c,images[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_HEAP_PROPERTIES rb={0};rb.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd={0};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=256*values[3];bd.Height=1;
        bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        OK(d->lpVtbl->CreateCommittedResource(d,&rb,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&readbacks[i]));
        D3D12_TEXTURE_COPY_LOCATION to={0},from={0};to.pResource=readbacks[i];to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Footprint.Format=td.Format;to.PlacedFootprint.Footprint.Width=values[2];to.PlacedFootprint.Footprint.Height=values[3];
        to.PlacedFootprint.Footprint.Depth=1;to.PlacedFootprint.Footprint.RowPitch=256;
        from.pResource=images[i];from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;c->lpVtbl->CopyTextureRegion(c,&to,0,0,0,&from,NULL);
    }
    OK(c->lpVtbl->Close(c));ID3D12CommandList*list=(ID3D12CommandList*)c;q->lpVtbl->ExecuteCommandLists(q,1,&list);
    ID3D12Fence*f=NULL;OK(d->lpVtbl->CreateFence(d,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&f));
    HANDLE event=CreateEvent(NULL,FALSE,FALSE,NULL);assert(event);OK(q->lpVtbl->Signal(q,f,1));OK(f->lpVtbl->SetEventOnCompletion(f,1,event));
    assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);CloseHandle(event);
    for(unsigned i=0;i<3;++i){
        void*data=NULL;D3D12_RANGE range={0,256*19*sy[i]};OK(readbacks[i]->lpVtbl->Map(readbacks[i],0,&range,&data));
        for(unsigned y=0;y<19*sy[i];++y)for(unsigned x=0;x<17*sx[i];++x){
            unsigned quadrant=(x/sx[i]>=8)+(y/sy[i]>=9)*2;
            unsigned char*b=(unsigned char*)data+y*256+x*4;unsigned got=((unsigned)b[3]<<16)|((unsigned)b[0]<<8)|b[1];
            unsigned expected=(unsigned)roundf(depths[quadrant]*16777215.0f);int error=(int)got-(int)expected;
            if(error < -1 || error > 1 || b[2]!=stencils[quadrant]){
                fprintf(stderr,"GPU mismatch %u: %u,%u depth=%06X/%06X stencil=%u/%u\n",i,x,y,got,expected,b[2],stencils[quadrant]);return 1;}
        }
        D3D12_RANGE none={0,0};readbacks[i]->lpVtbl->Unmap(readbacks[i],0,&none);
    }
    ID3D12InfoQueue*info=NULL;
    if(SUCCEEDED(d->lpVtbl->QueryInterface(d,&IID_ID3D12InfoQueue,(void**)&info))){
        for(UINT64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);++i){SIZE_T n=0;info->lpVtbl->GetMessage(info,i,NULL,&n);D3D12_MESSAGE*m=malloc(n);
            OK(info->lpVtbl->GetMessage(info,i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){fprintf(stderr,"GPU validation: %s\n",m->pDescription);return 1;}free(m);}
        DROP(info);
    }
    puts("PASS: production GPU pack shader, D24 depth + exact stencil, 1x/2x/4x storage footprints, partial dispatch bounds");
    for(unsigned i=0;i<3;++i){DROP(readbacks[i]);DROP(images[i]);}
    DROP(z);DROP(f);DROP(dsheap);DROP(heap);DROP(pso);DROP(root);DROP(c);DROP(a);DROP(q);DROP(d);DROP(adapter);DROP(factory);return 0;
}

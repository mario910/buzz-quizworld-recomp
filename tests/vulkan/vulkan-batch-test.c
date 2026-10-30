/* Real GPU contract test with entirely synthetic shaders/pixels. Including
 * the implementation lets this fixture test the backend directly, without
 * generating a retail command stream or exposing test APIs to the game. */
#include "../../ps3recomp/libs/video/rsx_vulkan_backend.c"
#include <math.h>
unsigned char* vm_base;
struct batch_fentry {uint64_t address;void* function;const char* name;};
const struct batch_fentry function_table[]={{0,NULL,NULL}};
const uint64_t function_table_count=0;
static void env(const char* name,const char* value){
#ifdef _WIN32
    _putenv_s(name,value);
#else
    setenv(name,value,1);
#endif
}
static void check(int value,const char* message){if(!value){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
#define W 64u
#define H 32u
static const char vs[]=
    "cbuffer VPConst : register(b0) { float4 shift; };"
    "float4 main(float4 p : ATTR0) : SV_Position { return p + float4(shift.xy, 0, 0); }";
static const char ps[]=
    "cbuffer PSConstants : register(b1) { float4 tint; };"
    "Texture2D rsx_tex[16] : register(t0); SamplerState rsx_samp[16] : register(s0);"
    "float4 main() : SV_Target0 { return rsx_tex[0].SampleLevel(rsx_samp[0], float2(0.5,0.5),0)*tint; }";
static const char ps_mrt[]=
    "cbuffer PSConstants : register(b1) { float4 tint; };"
    "struct O { float4 a : SV_Target0; float4 b : SV_Target1; };"
    "O main() { O o; o.a=tint; o.b=float4(0,0,1,1); return o; }";
static const char ps_tint[]=
    "cbuffer PSConstants : register(b1) { float4 tint; };"
    "float4 main() : SV_Target0 { return tint; }";
static u8 pixels[W*H*4];
static void pixel(u32 x,u32 y,u32 r,u32 g,u32 b){
    const u8* p=pixels+(y*W+x)*4;
    if(abs((int)p[0]-(int)r)>1||abs((int)p[1]-(int)g)>1||abs((int)p[2]-(int)b)>1){
        fprintf(stderr,"Pixel (%u,%u): %u,%u,%u expected %u,%u,%u\n",x,y,p[0],p[1],p[2],r,g,b);exit(1);
    }
}
static void read_target(u32 rt){memset(pixels,0xcd,sizeof pixels);vk_eng_readback(NULL,rt,0,0,W,H,pixels,W*4);}
static void constants(float shift,float r,float g,float b){
    const float v[4]={shift,0,0,0},p[4]={r,g,b,1};
    vk_eng_bind_vs_constants(NULL,v,sizeof v);vk_eng_bind_ps_constants(NULL,p,sizeof p);
}
static void quad(float* out,u32 x,u32 y,u32 w,u32 h,float shift,float z){
    const float l=(float)x*2/W-1-shift,r=(float)(x+w)*2/W-1-shift;
    const float t=1-(float)y*2/H,b=1-(float)(y+h)*2/H;
    const float data[16]={l,t,z,1,r,t,z,1,l,b,z,1,r,b,z,1};memcpy(out,data,sizeof data);
}
static void bind_texture(u32 handle){
    u32 handles[RSX_BE_MAX_TEXTURES]={0};rsx_be_sampler_desc samplers[RSX_BE_MAX_TEXTURES]={0};
    handles[0]=handle;samplers[0].wrap_s=samplers[0].wrap_t=samplers[0].wrap_r=1;
    vk_eng_bind_textures(NULL,handles,samplers,1);
}
static u32 pipeline_state(const char* fragment,u32 rt_count,int depth,int blend){
    rsx_vertex_layout_plan layout={0};layout.mask=1;layout.stride=16;layout.count=1;layout.attrs[0]=0;
    memset(layout.slot_by_attr,-1,sizeof layout.slot_by_attr);layout.slot_by_attr[0]=0;
    rsx_be_render_state state={0};state.color_mask=0xffffffffu;state.color_mask_mrt=0xffffffffu;
    state.depth_test=depth;state.depth_write=depth;state.depth_func=0x0201;
    state.blend_enable=blend;state.sf_rgb=state.sf_a=0x0302;
    state.df_rgb=state.df_a=0x0303;state.eq_rgb=state.eq_a=0x8006;
    const u32 handle=vk_eng_pipeline_create(NULL,vs,fragment,&state,&layout,16,RSX_BE_FMT_R8G8B8A8,rt_count);
    check(handle!=0,"synthetic pipeline creation");return handle;
}
static u32 pipeline(const char* fragment,u32 rt_count,int depth){return pipeline_state(fragment,rt_count,depth,0);}
static void draw_quad(float* vertices,u32 count,const u32* indices){
    vk_eng_draw(NULL,RSX_TOPOLOGY_TRIANGLES,vertices,count,16,indices,6);
}
/* Retail shaders carry a full interpolant bank while fetching sparse vertex
 * attributes. Constant-UV smoke shaders cannot detect locations being compacted
 * differently between stages. Match the decompilers' interface field order. */
static void varying_uv_contract(u32 target)
{
    const char* vertex=
        "struct I { float4 p:ATTR0; float4 uv:ATTR8; };"
        "struct O { float4 p:SV_Position; float4 c0:COLOR0; float4 c1:COLOR1; float4 fog:FOG;"
        "float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;"
        "float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7;"
        "float4 t8:TEXCOORD8; float4 t9:TEXCOORD9; };"
        "O main(I i) { O o; o.p=i.p; o.c0=o.c1=o.fog=0.125;"
        "o.t0=o.t1=o.t2=o.t4=o.t5=o.t6=o.t7=o.t8=o.t9=0.125; o.t3=i.uv; return o; }";
    const char* fragment=
        "struct I { float4 p:SV_Position; float4 c0:COLOR0; float4 c1:COLOR1; float4 fog:FOG;"
        "float4 t0:TEXCOORD0; float4 t1:TEXCOORD1; float4 t2:TEXCOORD2; float4 t3:TEXCOORD3;"
        "float4 t4:TEXCOORD4; float4 t5:TEXCOORD5; float4 t6:TEXCOORD6; float4 t7:TEXCOORD7; };"
        "Texture2D rsx_tex[16]:register(t0); SamplerState rsx_samp[16]:register(s0);"
        "float4 main(I i):SV_Target0 { return rsx_tex[0].SampleLevel(rsx_samp[0],i.t3.xy,0); }";
    rsx_vertex_layout_plan layout={0};layout.mask=0x101;layout.stride=32;layout.count=2;
    memset(layout.slot_by_attr,-1,sizeof layout.slot_by_attr);layout.attrs[0]=0;layout.attrs[1]=8;
    layout.slot_by_attr[0]=0;layout.slot_by_attr[8]=1;
    rsx_be_render_state state={0};state.color_mask=state.color_mask_mrt=0xffffffffu;
    const u32 pipe=vk_eng_pipeline_create(NULL,vertex,fragment,&state,&layout,32,RSX_BE_FMT_R8G8B8A8,1);
    check(pipe!=0,"sparse-attribute varying pipeline");
    const u8 data[16]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};
    const u32 texture=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,2,2,1,1,0xaae4,0x85);
    check(texture!=0,"UV pattern texture");vk_eng_texture_upload(NULL,texture,0,0,2,2,data,8,2);
    const float vertices[32]={-1,1,0.5f,1, 0,0,0,1, 1,1,0.5f,1, 1,0,0,1,
                              -1,-1,0.5f,1, 0,1,0,1, 1,-1,0.5f,1, 1,1,0,1};
    const u32 indices[6]={0,1,2,2,1,3};
    vk_eng_bind_pipeline(NULL,pipe);vk_eng_bind_targets(NULL,&target,1,0);bind_texture(texture);
    vk_eng_draw(NULL,RSX_TOPOLOGY_TRIANGLES,vertices,4,32,indices,6);
    read_target(target);
    pixel(W/4,H/4,255,0,0);pixel(W*3/4,H/4,0,255,0);
    pixel(W/4,H*3/4,0,0,255);pixel(W*3/4,H*3/4,255,255,0);
    vk_eng_pipeline_release(NULL,pipe);vk_eng_release(NULL,texture);
}
static void texture_pool_contract(u32 target,u32 pipe)
{
    if(!s_texture_pool.on)return;
    const u32 indices[6]={0,1,2,2,1,3};float vertices[16];
    quad(vertices,0,0,W,H,0,0.5f);constants(0,1,1,1);
    vk_eng_bind_pipeline(NULL,pipe);vk_eng_bind_targets(NULL,&target,1,0);
    u8 rows[17*9*4];
    u32 t=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,17,9,1,1,0xaae4,0x85);
    check(t!=0,"pool contract texture");
    const VkImage image=s_eobj[t].im.img;
    const u64 hits=s_texture_pool.hits;
    for(u32 k=0;k<8;k++) {
        for(u32 p=0;p<17*9;p++) {
            rows[p*4]=(k&1)?0:255;rows[p*4+1]=(k&1)?255:0;
            rows[p*4+2]=0;rows[p*4+3]=255;
        }
        vk_eng_texture_upload(NULL,t,0,0,17,9,rows,17*4,9);
        memset(rows,0,sizeof rows); /* caller memory may immediately change */
        bind_texture(t);draw_quad(vertices,4,indices);
        vk_eng_release(NULL,t);
        check(!s_batch.recording && !s_batch.context_inflight[0] && !s_batch.context_inflight[1],
              "texture retirement drains every pending context before pooling");
        t=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,17,9,1,1,0xaae4,0x85);
        check(t && s_eobj[t].im.img==image,"identical retired image actually reused");
        read_target(target);pixel(W/2,H/2,(k&1)?0:255,(k&1)?255:0,0);
    }
    check(s_texture_pool.hits>=hits+8,"pool hit accounting");
    vk_eng_release(NULL,t);

    t=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,8,8,1,1,0xaae4,0x85);
    check(t!=0,"pool key reference");
    const VkImage original=s_eobj[t].im.img;vk_eng_release(NULL,t);
    const struct {rsx_be_format f;u32 w,h,mips,faces,remap,rsx;} keys[]={
        {RSX_BE_FMT_R8G8B8A8,9,8,1,1,0xaae4,0x85},
        {RSX_BE_FMT_R8G8B8A8,8,9,1,1,0xaae4,0x85},
        {RSX_BE_FMT_R8G8B8A8,8,8,2,1,0xaae4,0x85},
        {RSX_BE_FMT_R8G8B8A8,8,8,1,6,0xaae4,0x85},
        {RSX_BE_FMT_R8G8B8A8,8,8,1,1,0xaa1b,0x85},
        {RSX_BE_FMT_R8G8B8A8,8,8,1,1,0xaae4,0x9e},
        {RSX_BE_FMT_R8,8,8,1,1,0xaae4,0x81},
    };
    for(u32 i=0;i<sizeof keys/sizeof keys[0];i++) {
        /* Keep the reference live while testing incompatible identities, so
         * driver handle recycling cannot make a false equality assertion. */
        t=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,8,8,1,1,0xaae4,0x85);
        check(t && s_eobj[t].im.img==original,"reference pool entry retained");
        const u32 other=vk_eng_texture_create(NULL,keys[i].f,keys[i].w,keys[i].h,
            keys[i].mips,keys[i].faces,keys[i].remap,keys[i].rsx);
        check(other && s_eobj[other].im.img!=original,"all image/view key fields respected");
        vk_eng_release(NULL,other);vk_eng_release(NULL,t);
    }
    for(u32 i=0;i<12;i++) {
        t=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,32+i,16,1,1,0xaae4,0x85);
        check(t!=0,"pool count pressure");vk_eng_release(NULL,t);
        check(s_texture_pool.count<=VK_TEXTURE_POOL_COUNT &&
              s_texture_pool.bytes<=VK_TEXTURE_POOL_BYTES,"pool count/byte limits");
    }
    const u64 evicted=s_texture_pool.evicted;
    for(u32 i=0;i<5;i++) {
        t=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,2048+i,2048,1,1,0xaae4,0x85);
        check(t!=0,"pool byte pressure");vk_eng_release(NULL,t);
        check(s_texture_pool.count<=VK_TEXTURE_POOL_COUNT &&
              s_texture_pool.bytes<=VK_TEXTURE_POOL_BYTES,"actual allocation byte limit");
    }
    check(s_texture_pool.evicted>evicted && s_texture_pool.count<VK_TEXTURE_POOL_COUNT,
          "byte capacity evicts before object capacity");
}
static void depth_sample_contract(u32 target,u32 pipe)
{
    const u32 depth=vk_eng_depth_target_create(NULL,W,H);
    check(depth!=0,"readback depth target");
    vk_eng_clear_depth_stencil(NULL,depth,RSX_BE_CLEAR_DEPTH|RSX_BE_CLEAR_STENCIL,0.25f,7);
    float rows[36];for(u32 i=0;i<36;i++)rows[i]=-17;
    check(!vk_eng_depth_readback(NULL,depth,5,3,7,3,rows,12*sizeof(float)),"typed depth readback");
    for(u32 y=0;y<3;y++)for(u32 x=0;x<12;x++)
        check(x<7?fabsf(rows[y*12+x]-0.25f)<0.00002f:rows[y*12+x]==-17,
              "normalized depth values and untouched output row padding");
    float odd[30];memset(odd,0xcd,sizeof odd);
    check(!vk_eng_depth_readback(NULL,depth,5,3,7,3,odd,37),"unaligned subsequent row pitch");
    for(u32 y=0;y<3;y++) {
        for(u32 x=0;x<7;x++) {
            float f;memcpy(&f,(const u8*)odd+y*37+x*4,4);
            check(fabsf(f-0.25f)<0.00002f,"unaligned row conversion");
        }
        for(u32 x=28;x<37;x++)check(((const u8*)odd)[y*37+x]==0xcd,"odd-pitch padding guard");
    }
    const u32 snap=vk_eng_depth_snapshot(NULL,depth,W/2,H/2);
    check(snap!=0,"sampleable cropped depth snapshot");
    vk_eng_bind_pipeline(NULL,pipe);vk_eng_bind_targets(NULL,&target,1,0);bind_texture(snap);
    constants(0,1,1,1);float vertices[16];quad(vertices,0,0,W,H,0,0.5f);
    const u32 indices[6]={0,1,2,2,1,3};draw_quad(vertices,4,indices);
    vk_eng_clear_depth_stencil(NULL,depth,RSX_BE_CLEAR_DEPTH,0.75f,0);
    check(vk_eng_depth_snapshot(NULL,depth,W/2,H/2)==snap,"snapshot storage refilled after depth change");
    read_target(target);pixel(W/2,H/2,64,0,0);
    bind_texture(snap);draw_quad(vertices,4,indices);
    read_target(target);pixel(W/2,H/2,191,0,0);
    vk_eng_release(NULL,depth);
    check(!vk_eng_get(snap,VK_ENG_TEXTURE),"depth target owns snapshot lifetime");
}
int main(int argc,char** argv){
    if(argc==2 && !strcmp(argv[1],"--invalid-depth-format")){
        env("PS3RECOMP_VK_DEPTH_FORMAT","bad");
        const int result=rsx_vulkan_backend_init(W,H,"Invalid depth format");
        if(!result)rsx_vulkan_backend_shutdown();
        check(result!=0 && !s_vk.device && !s_vk.lib,"invalid depth rejected before device creation");
        puts("PASS invalid depth format rejected explicitly");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--invalid-texture-reuse")){
        env("PS3RECOMP_VK_TEXTURE_REUSE","bad");
        const int result=rsx_vulkan_backend_init(W,H,"Invalid texture reuse");
        if(!result)rsx_vulkan_backend_shutdown();
        check(result!=0 && !s_vk.device && !s_vk.lib,"invalid reuse rejected before device creation");
        puts("PASS invalid texture reuse rejected explicitly");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--invalid-async-timestamps")){
        env("PS3RECOMP_VK_DRAW_BATCH","64");env("PS3RECOMP_VK_DRAW_RING","1");
        env("PS3RECOMP_VK_DRAW_PASSES","1");env("PS3RECOMP_VK_DRAW_ASYNC","1");
        env("PS3RECOMP_VK_GPU_TIMESTAMPS","1");
        const int result=rsx_vulkan_backend_init(W,H,"Invalid async timestamp combination");
        if(!result)rsx_vulkan_backend_shutdown();
        check(result!=0 && s_batch.async_requested && s_gpu.requested && !s_vk.device && !s_vk.lib,
              "unsupported async timestamps rejected before device creation");
        puts("PASS unsupported async timestamp combination rejected explicitly");return 0;
    }
    check(vk_gpu_tick_delta(UINT64_MAX-1,2,UINT64_MAX)==4,"64-bit timestamp wrap");
    check(vk_gpu_tick_delta(0xfffffffeull,2,0xffffffffull)==4,"32-bit timestamp wrap");
    check(vk_gpu_tick_delta(0x1fffe,0x20001,0xffff)==3,"timestamp invalid upper bits masked");
    check(argc==2,"expected draw batch size argument");
    env("PS3RECOMP_VK_DRAW_BATCH",argv[1]);env("PS3RECOMP_VK_WINDOW","0");
    env("PS3RECOMP_VK_GUEST_PROGRAMS","1");env("PS3RECOMP_RSX_ENGINE","dispatch");
    vm_base=calloc(1,16u<<20);check(vm_base!=NULL,"synthetic VM");
    check(rsx_vulkan_backend_init(W,H,"Synthetic Vulkan draw batches")==0,"Vulkan initialization");
    if(s_gpu.requested && !s_gpu.pool){
        rsx_vulkan_backend_shutdown();free(vm_base);vm_base=NULL;
        puts("SKIP queue/device timestamps unavailable");return 77;
    }
    check(s_vk.eng_active,"real register-file engine");
    u32 rt=vk_eng_color_target_create(NULL,RSX_BE_FMT_R8G8B8A8,W,H,NULL,0);
    u32 copy=vk_eng_color_target_create(NULL,RSX_BE_FMT_R8G8B8A8,W,H,NULL,0);
    check(rt&&copy,"render targets");
    u32 tex[2];const u8 white[4]={255,255,255,255},gray[4]={128,128,128,255};
    for(u32 i=0;i<2;i++){
        tex[i]=vk_eng_texture_create(NULL,RSX_BE_FMT_R8G8B8A8,1,1,1,1,0xaae4,0x85);
        check(tex[i]!=0,"texture allocation");vk_eng_texture_upload(NULL,tex[i],0,0,1,1,i?gray:white,4,1);
    }
    u32 pipe=pipeline(ps,1,0);vk_eng_bind_pipeline(NULL,pipe);vk_eng_bind_targets(NULL,&rt,1,0);
    const u32 tiles=s_batch.limit>128?s_batch.limit+3:32;
    for(u32 i=0;i<tiles;i++){
        float vertices[32];const float shift=(i&1)?0.25f:0;
        quad(vertices,(i%8)*8,((i/8)%4)*8,8,8,shift,0.5f);
        quad(vertices+16,0,0,1,1,shift,0.5f);
        const u32 off=(i&1)?4:0;
        if(off){float temp[16];memcpy(temp,vertices,sizeof temp);memcpy(vertices,vertices+16,sizeof temp);memcpy(vertices+16,temp,sizeof temp);}
        u32 indices[6]={off,off+1,off+2,off+2,off+1,off+3};
        constants(shift,i%3==0,i%3==1,i%3==2);bind_texture(tex[i&1]);
        draw_quad(vertices,8,indices);
        rsx_draw_engine_method(0x1d74,1); // release the captured guest input ring
        /* The caller may immediately reuse its temporary list. */
        memset(vertices,0,sizeof vertices);memset(indices,0,sizeof indices);
    }
    if(s_batch.limit>1 && s_batch.retire_sources)check(s_batch.recording,"draws really queued before the readback");
    // Upcoming constants must survive an older batch writing its shared GPU UBO.
    constants(0.25f,1,1,0);
    read_target(rt);
    check(!s_batch.recording,"readback flush boundary");
    for(u32 i=0;i<32;i++){
        const u32 last=i+((tiles-1-i)/32)*32;
        u32 rgb[3]={0};rgb[last%3]=(last&1)?128:255;
        pixel((i%8)*8+4,(i/8)*8+4,rgb[0],rgb[1],rgb[2]);
    }
    float vertices[16];const u32 indices[6]={0,1,2,2,1,3};
    vk_eng_bind_targets(NULL,&copy,1,0);bind_texture(tex[0]);quad(vertices,0,0,W,H,0.25f,0.5f);
    draw_quad(vertices,4,indices);read_target(copy);pixel(W/2,H/2,255,255,0);
    // Render-to-texture view: no explicit CPU wait between producer and consumer.
    constants(0,1,0,0);vk_eng_bind_targets(NULL,&rt,1,0);quad(vertices,0,0,W,H,0,0.5f);
    draw_quad(vertices,4,indices);
    const u32 view=vk_eng_surface_view(NULL,rt,0xaae4,0x85);check(view,"RTT view");
    constants(0,1,1,1);bind_texture(view);vk_eng_bind_targets(NULL,&copy,1,0);
    draw_quad(vertices,4,indices);read_target(copy);pixel(W/2,H/2,255,0,0);
    vk_eng_release(NULL,view);
    // Pending descriptors must finish before a sampled texture is destroyed.
    bind_texture(tex[0]);constants(0,0,1,0);draw_quad(vertices,4,indices);
    vk_eng_release(NULL,tex[0]);read_target(copy);pixel(W/2,H/2,0,255,0);
    // A pending render pass must end before a transfer clear uses its target.
    bind_texture(tex[1]);constants(0,1,0,0);draw_quad(vertices,4,indices);
    const float cyan[4]={0,1,1,1};
    vk_eng_clear_color(NULL,copy,cyan);read_target(copy);pixel(W/2,H/2,0,255,255);
    bind_texture(tex[1]);
    // Boundary and oversized vertex payloads, including synchronous fallback.
    const u32 counts[]={4096,4097,VK_BATCH_RING_GEOMETRY_BYTES/16,VK_BATCH_RING_GEOMETRY_BYTES/16+1};
    for(u32 i=0;i<sizeof(counts)/sizeof(counts[0]);i++){
        const u32 count=counts[i];
        float* large=calloc((size_t)count,16);check(large!=NULL,"large vertices");quad(large,0,0,W,H,0,0.5f);
        constants(0,0,0,1);const u64 before=s_batch.total_draws;
        draw_quad(large,count,indices);free(large);
        const int queued=s_batch.limit && ((u64)count*16 <=
            (s_batch.ring_requested ? VK_BATCH_RING_GEOMETRY_BYTES : 65536u));
        check(s_batch.total_draws==before+(queued?1:0),"payload capacity and synchronous fallback boundary");
        read_target(copy);pixel(W/2,H/2,0,0,128);
    }
    // Geometry space, not the descriptor count, can also force a batch fence.
    if(s_batch.ring_ready && s_batch.limit>1){
        const u32 count=VK_BATCH_RING_GEOMETRY_BYTES/32+1;
        float* large=calloc((size_t)count,16);check(large!=NULL,"ring wrap vertices");
        quad(large,0,0,W,H,0,0.5f);const u64 fences=s_batch.flushes;
        constants(0,1,0,0);draw_quad(large,count,indices);
        constants(0,0,1,0);draw_quad(large,count,indices);free(large);
        check(s_batch.flushes>fences,"geometry capacity flushed before slice reuse");
        read_target(copy);pixel(W/2,H/2,0,128,0);
    }
    // Guest completion notifications must flush before the FIFO publishes them.
    const u32 methods[]={0x50,0x14,0x6c,0x104,0x1d70,0x1d74,0x1800};
    for(u32 i=0;i<sizeof(methods)/sizeof(methods[0]);i++){
        constants(0,1,0,1);draw_quad(vertices,4,indices);
        const int queued=s_batch.recording;
        rsx_draw_engine_method(methods[i],1);
        const int captured=methods[i]==0x1d74 && s_batch.retire_sources && s_batch.limit && !s_batch.failed;
        check(s_batch.recording==(captured?queued:0),"guest completion versus input-retirement boundary");
    }
    // Depth hazards between queued draws, followed by pipeline release.
    u32 depth=vk_eng_depth_target_create(NULL,W,H);check(depth!=0,"depth target");
    vk_eng_bind_targets(NULL,&copy,1,depth);
    u32 depth_pipe=pipeline(ps,1,1);vk_eng_bind_pipeline(NULL,depth_pipe);
    constants(0,1,0,0);quad(vertices,0,0,W,H,0,0.25f);draw_quad(vertices,4,indices);
    constants(0,0,0,1);quad(vertices,0,0,W,H,0,0.75f);draw_quad(vertices,4,indices);
    read_target(copy);pixel(W/2,H/2,128,0,0);
    constants(0,1,0,0);quad(vertices,0,0,W,H,0,0.125f);draw_quad(vertices,4,indices);
    vk_eng_clear_depth_stencil(NULL,depth,RSX_BE_CLEAR_DEPTH,1.0f,0);
    constants(0,0,0,1);quad(vertices,0,0,W,H,0,0.75f);draw_quad(vertices,4,indices);
    vk_eng_pipeline_release(NULL,depth_pipe);read_target(copy);pixel(W/2,H/2,0,0,128);
    vk_eng_bind_targets(NULL,&copy,1,0);vk_eng_release(NULL,depth);
    // Blending in a shared pass must preserve order across distinct draws.
    const float black[4]={0,0,0,1},red_half[4]={1,0,0,0.5f},blue_half[4]={0,0,1,0.5f};
    vk_eng_clear_color(NULL,copy,black);
    u32 blend=pipeline_state(ps_tint,1,0,1);vk_eng_bind_pipeline(NULL,blend);
    constants(0,1,1,1);quad(vertices,0,0,W,H,0,0.5f);
    vk_eng_bind_ps_constants(NULL,red_half,sizeof red_half);draw_quad(vertices,4,indices);
    vk_eng_bind_ps_constants(NULL,blue_half,sizeof blue_half);draw_quad(vertices,4,indices);
    read_target(copy);pixel(W/2,H/2,64,0,128);vk_eng_pipeline_release(NULL,blend);
    // Two color outputs must retain independent attachments and descriptors.
    u32 mrt=pipeline(ps_mrt,2,0),targets[2]={rt,copy};vk_eng_bind_pipeline(NULL,mrt);
    vk_eng_bind_targets(NULL,targets,2,0);constants(0,0,1,0);quad(vertices,0,0,W,H,0,0.5f);
    draw_quad(vertices,4,indices);read_target(rt);pixel(W/2,H/2,0,255,0);
    read_target(copy);pixel(W/2,H/2,0,0,255);
    check(!s_batch.failed,"batch stayed valid");
    if(s_batch.limit && s_batch.ring_requested)
        check(s_batch.ring_ready && !s_batch.ring_failed,"real mapped draw slices used (not fallback)");
    if(s_batch.limit>1 && s_batch.ring_requested && s_batch.merge_passes && s_batch.retire_sources)
        check(s_batch.reused_passes>0,"successive draws actually reused their render pass");
    check(!s_batch.pass_active,"readback completed the pending render pass");
    check(!s_batch.context_inflight[0] && !s_batch.context_inflight[1],"readback drained older async contexts too");
    if(s_batch.async_requested)
        check(s_batch.async_ready && s_batch.async_submissions>0,"actual double-buffered async submissions");
    if(s_batch.limit && s_batch.ring_requested && s_batch.merge_passes && s_batch.batch_clears)
        check(s_batch.queued_clears>0,"color/depth clears actually queued");
    if(s_gpu.requested)
        check(s_gpu.pool && s_gpu.samples>0 && s_gpu.draw_samples>0 && s_gpu.total_ns>0 && !s_gpu.failures,
              "actual device timestamps collected after draw fences");
    if(s_batch.limit)check(s_batch.total_draws>32&&s_batch.flushes>0,"actual batched draws/fences");
    texture_pool_contract(copy,pipe);
    depth_sample_contract(copy,pipe);
    varying_uv_contract(copy);
    rsx_vulkan_backend_shutdown();
    check(!s_texture_pool.count && !s_texture_pool.bytes,"shutdown frees all retired pool images");
    free(vm_base);vm_base=NULL;
    puts("PASS draw payload snapshots, immutable descriptors, constants across flush, RTT, lifetime, fallback, guest labels, depth and MRT");return 0;
}

#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_ring.h"
#include "../port/native_geometry_job_io.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void buzz_geometry_job_native_fail(spu_context*,const char* message){fprintf(stderr,"ring reference failure:%s\n",message);abort();}
}
#include "../analysis/native-geometry/phase3/test-ring-reference.inc"
extern "C" void spu_indirect_branch(spu_context* c){assert(c->pc==0x3948);ring_oracle::buzz_job_6eba_spu_func_00003948(c);}

static std::vector<GeometryJobIoOperation> transcript;
static GeometryJobIoReplay* replay;
static uint8_t lane[128],shared[128];
static std::vector<uint32_t> free_values;
static unsigned free_cursor,failures_left,command_count,checks;
static unsigned current_trial;
static uint32_t ring_ea=0x1000000,lane_ea;
static unsigned visited[0x4100]{};
static uint32_t word(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static void word(uint8_t* p,uint32_t v){p[0]=uint8_t(v>>24);p[1]=uint8_t(v>>16);p[2]=uint8_t(v>>8);p[3]=uint8_t(v);}
static uint32_t rng(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
static void record_transfer(GeometryJobIoOperation& op,spu_context& c,unsigned kind,unsigned lsa,unsigned ea,unsigned n)
{
    assert(lsa<=SPU_LS_SIZE && n<=SPU_LS_SIZE-lsa);
    op.transfers.push_back({kind,lsa,ea,{c.ls+lsa,c.ls+lsa+n}});
}
extern "C" void buzz_geometry_job_wrch(spu_context* c,uint32_t channel,u128 argument)
{
    u128 ignored{};
    if(replay){
        const size_t at=replay->consumed();
        if(!replay->consume(*c,GeometryJobIoKind::write,channel,argument._u32[0],ignored)){
            fprintf(stderr,"ring trial%u op%zu channel%u argument%X error%s expected-channel%u/argument%X\n",current_trial,at,channel,argument._u32[0],replay->error(),at<transcript.size()?transcript[at].channel:999,at<transcript.size()?transcript[at].argument:0);abort();
        }return;
    }
    auto& op=transcript.emplace_back();op.kind=GeometryJobIoKind::write;op.channel=channel;op.argument=argument._u32[0];op.before=GeometryJobControls::capture(*c);
    const uint32_t v=op.argument;
    switch(channel){
        case MFC_LSA:c->mfc_lsa=v&SPU_LS_MASK;break;
        case MFC_EAL:c->mfc_eal=v;break;
        case MFC_Size:c->mfc_size=v;break;
        case MFC_TagID:c->mfc_tag=v&31;break;
        case MFC_WrTagMask:c->mfc_tag_mask=v;break;
        case MFC_WrTagUpdate:c->mfc_tag_status=c->mfc_tag_mask;break;
        case MFC_Cmd:{
            assert(++command_count<10000);
            if(v==0xd0){
                auto* source=(c->mfc_eal&~127u)==lane_ea?lane:shared;assert((c->mfc_eal&~127u)==lane_ea || (c->mfc_eal&~127u)==ring_ea);
                const unsigned lsa=c->mfc_lsa&~127u;memcpy(c->ls+lsa,source,128);c->resv_valid=1;c->resv_ea=c->mfc_eal&~127u;memcpy(c->resv_line,source,128);c->atomic_stat=0;
                record_transfer(op,*c,3,lsa,c->resv_ea,128);
            }else if(v==0xb0 || v==0xb4){
                auto* target=(c->mfc_eal&~127u)==lane_ea?lane:shared;assert((c->mfc_eal&~127u)==lane_ea || (c->mfc_eal&~127u)==ring_ea);
                const unsigned lsa=c->mfc_lsa&~127u;record_transfer(op,*c,4,lsa,c->mfc_eal&~127u,128);
                const bool fail=v==0xb4 && failures_left;if(fail){--failures_left;shared[100]^=0x91;}else memcpy(target,c->ls+lsa,128);
                c->atomic_stat=fail?1:0;c->resv_valid=0;
            }else{
                assert(v==0x40 && c->mfc_size==4 && !free_values.empty());const unsigned i=free_cursor<free_values.size()?free_cursor:unsigned(free_values.size()-1);++free_cursor;
                word(c->ls+c->mfc_lsa,free_values[i]);record_transfer(op,*c,1,c->mfc_lsa,c->mfc_eal,4);
            }
            break;
        }
        default:abort();
    }
    op.after=GeometryJobControls::capture(*c);
}
extern "C" u128 buzz_geometry_job_rdch(spu_context* c,uint32_t channel)
{
    u128 value{};
    if(replay){assert(replay->consume(*c,GeometryJobIoKind::read,channel,0,value));return value;}
    assert(transcript.size()<60000);auto& op=transcript.emplace_back();op.kind=GeometryJobIoKind::read;op.channel=channel;op.before=GeometryJobControls::capture(*c);
    switch(channel){
        case SPU_RdMachStat:value=spu_pref_u32(c->int_enable&1);break;
        case MFC_RdAtomicStat:value=spu_pref_u32(c->atomic_stat);break;
        case MFC_RdTagStat:value=spu_pref_u32(c->mfc_tag_status);break;
        case SPU_RdDec:value=spu_pref_u32(c->decrementer--);break;
        default:abort();
    }
    op.result=value;op.after=GeometryJobControls::capture(*c);return value;
}

static void seed(spu_context& c,unsigned trial,unsigned scenario,unsigned bias=0)
{
    memset(&c,0,sizeof c);c.ls=c.ls_store;c.image_id=41;c.pc=0x37c0;uint32_t state=trial+1;
    for(auto& reg:c.gpr)for(auto& v:reg._u32)v=rng(state);
    for(unsigned at=0;at<SPU_LS_SIZE;at+=4){const uint32_t v=rng(state);memcpy(c.ls+at,&v,4);}
    for(auto* line:{lane,shared})for(unsigned at=0;at<128;at+=4)word(line+at,rng(state));
    c.gpr[1]._u32[0]=0x30000+bias;c.gpr[3]._u32[0]=0x20000+bias;c.gpr[4]._u32[0]=ring_ea|(trial&1);
    c.gpr[5]._u32[0]=1+(trial%128);c.gpr[6]._u32[0]=0x21000+bias;c.gpr[7]._u32[0]=trial%6;
    c.gpr[0]._u32[0]=trial%4==0?0x38d0:0x3ff00;c.host_depth=trial%3;c.decrementer=trial*17;
    c.resv_valid=trial&1;c.resv_ea=0x713000;c.event_status=trial*7;c.event_mask=trial*3;c.mfc_tag_status=trial*123;
    for(auto& v:c.resv_line)v=uint8_t(rng(state));
    word(c.ls+((c.gpr[3]._u32[0]+0x150)&0x3fff0)+4,trial%32);
    const unsigned begin=0x200000,end=begin+0x4000,cursor=begin+1024;
    word(lane,begin);word(lane+4,0x8000);word(lane+8,end);word(lane+12,cursor);word(lane+16,0x7f318bad);
    word(lane+20,0x400000+4*(trial%32));word(lane+24,end);
    word(shared,0x300000);word(shared+4,0xc000);word(shared+8,0x320000);word(shared+12,0x300200);word(shared+16,0x6391abc7);word(shared+20,16);
    lane_ea=ring_ea+128+128*c.gpr[7]._u32[0];free_cursor=0;failures_left=trial%3;command_count=0;free_values={end};
    switch(scenario){
        case 0:c.gpr[4]._u32[0]=0xfff00102|(trial&1);c.gpr[5]._u32[0]=rng(state);break;
        case 1:break;
        case 2:word(lane+12,end-64);word(lane+24,begin+4096);break;
        case 3:word(lane+24,0);break;
        case 4:word(lane+24,cursor+64);free_values={cursor+64};break;
        case 5:word(lane+8,begin);break;
        case 6:word(lane+8,begin);word(shared+8,0x300240);break;
        case 7:word(lane+8,begin+128);c.gpr[5]._u32[0]=256;break;
        case 8:word(lane+8,begin+128);c.gpr[5]._u32[0]=256;word(shared+8,0x300240);break;
        case 9:word(lane+24,cursor+64);word(shared+8,0x300240);free_values={cursor+64,end};break;
        case 10:word(lane+24,cursor+64);word(shared+8,0x300240);free_values={cursor+64,cursor+64,end};break;
        case 11:word(lane+24,begin-4);break;
        case 12:word(lane+24,end+4);break;
        default:abort();
    }
}
static uint32_t original(spu_context& c)
{
    ++visited[0x37c0];g_spu_trampoline_fn=nullptr;ring_oracle::buzz_job_6eba_spu_func_000037C0(&c);
    for(unsigned steps=0;;++steps){
        assert(steps<100000);auto next=g_spu_trampoline_fn;assert(next);
        if((c.pc==0x3dd0 && next==ring_oracle::buzz_job_6eba_spu_func_00003DD0) ||
           (c.pc==0x4078 && next==ring_oracle::buzz_job_6eba_spu_func_00004078)){++visited[c.pc];return c.pc;}
        if(next==spu_indirect_branch){assert(c.pc==0x3948);next=ring_oracle::buzz_job_6eba_spu_func_00003948;}
        if(c.pc<0x4100)++visited[c.pc];g_spu_trampoline_fn=nullptr;next(&c);
        if(next==ring_oracle::buzz_job_6eba_spu_func_000038D0)return 0;
    }
}
static void compare(spu_context& native,spu_context& reference,unsigned trial)
{
    auto input=std::make_unique<spu_context>();memcpy(input.get(),&native,sizeof native);input->ls=input->ls_store;
    current_trial=trial;transcript.clear();const unsigned ref_end=original(reference);const auto continuation=g_spu_trampoline_fn;
    uint8_t external[256];memcpy(external,lane,128);memcpy(external+128,shared,128);const unsigned actual_commands=command_count,actual_reads=free_cursor;
    GeometryRing plan;assert(read_geometry_ring(native,plan));GeometryJobIoReplay io(transcript);replay=&io;
    const GeometryRingContinuations f{ring_oracle::buzz_job_6eba_spu_func_00003DD0,ring_oracle::buzz_job_6eba_spu_func_00004078};
    g_spu_trampoline_fn=spu_indirect_branch;const unsigned native_end=allocate_geometry_ring(native,plan,f);replay=nullptr;
    assert(io.finish());const auto difference=compare_function_state(native,reference);
    if(native_end!=ref_end || !difference.exact()){
        fprintf(stderr,"ring trial%u frontier%X/%X r%d byte%d LS%X/%u state%s\n",trial,native_end,ref_end,difference.first_register,difference.first_register_byte,difference.first_ls,difference.ls_bytes,difference.first_state?difference.first_state:"equal");abort();
    }
    assert(g_spu_trampoline_fn==continuation);
    assert(native.image_id==reference.image_id && native.host_depth==reference.host_depth && native.policy_mode==reference.policy_mode && native.resident_ovl==reference.resident_ovl);
    // Exercise the production root drainer, including both post-DMA reporting
    // handoffs, on a fresh replay cursor without repeating external effects.
    GeometryJobIoReplay root_io(transcript);replay=&root_io;
    const buzz_geometry_ring_functions library{ring_oracle::buzz_job_6eba_spu_func_000038D0,f.initial_report,f.shared_report};
    run_geometry_ring_reference(*input,ring_oracle::buzz_job_6eba_spu_func_000037C0,library,native_end);replay=nullptr;
    assert(root_io.finish() && compare_function_state(*input,native).exact() && g_spu_trampoline_fn==continuation);
    assert(input->image_id==native.image_id && input->host_depth==native.host_depth && input->policy_mode==native.policy_mode && input->resident_ovl==native.resident_ovl);
    assert(actual_commands==command_count && actual_reads==free_cursor && !memcmp(external,lane,128) && !memcmp(external+128,shared,128));
    ++checks;
}
int main()
{
    auto native=std::make_unique<spu_context>(),reference=std::make_unique<spu_context>();
    seed(*native,1,1);ring_oracle::load_real(*native,"pc_37C0.bin");native->pc=0x21f8;
    reference->ls=reference->ls_store;ring_oracle::load_real(*reference,"pc_3948_exit.bin");memcpy(lane,reference->ls+0xe600,128);
    ring_ea=native->gpr[4]._u32[0]&~127u;lane_ea=ring_ea+128+128*native->gpr[7]._u32[0];failures_left=0;
    memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,99999);
    ring_oracle::load_real(*reference,"pc_38D0_exit.bin");
    assert(!memcmp(native->gpr,reference->gpr,sizeof native->gpr) && !memcmp(native->ls,reference->ls,SPU_LS_SIZE) && native->pc==0x2210);
    ring_ea=0x1000000;
    for(unsigned trial=0;trial<13*64;++trial){
        seed(*native,trial,trial%13,(trial/13)%16);memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;
        compare(*native,*reference,trial);
    }
    // Raw u32 wrapping and late frame/result aliases: never compare with a
    // precomputed copy of the saved registers instead of loading them late.
    const uint32_t stacks[]={0,16,0x100,0xe680,0xe900,0x3fff0,0xfffffff0,0x80030000};
    const uint32_t outputs[]={0,0xe600,0xe610,0x3fff0,0xfffffff0,0x21000};
    for(unsigned trial=0;trial<4096;++trial){
        const unsigned scenario=trial%3;seed(*native,trial+10000,scenario,trial%16);
        native->gpr[1]._u32[0]=stacks[(trial/3)%8]+trial%16;
        native->gpr[6]._u32[0]=trial%7==6?native->gpr[1]._u32[0]+16:outputs[(trial/24)%6]+trial%16;
        if(scenario==1)native->gpr[5]._u32[0]=1+trial%8192;
        memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,trial+10000);
    }
    // A shared scratch line may alias E600; then the post-fallback cursor
    // validation must see the DMA's replacement bytes, and stop at 4078.
    for(unsigned trial=0;trial<32;++trial){
        seed(*native,trial+20000,9);native->gpr[1]._u32[0]=0xe900;
        word(shared+24,trial&1?0x2ffffc:0x330000);
        memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,trial+20000);
        assert(native->pc==0x4078);
    }
    for(unsigned trial=0;trial<64;++trial){
        seed(*native,trial+21000,9);word(lane+12,0x203fc0);word(lane+24,0x200040);
        free_values=trial&1?std::vector<uint32_t>{0x200040,0x200040,0x201000}:std::vector<uint32_t>{0x200040,0x201000};
        memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,trial+21000);
    }
    const uint32_t requests[]={0,1,127,128,129,8191,8192,8193,0x7fffffff,0x80000000,0xffffff80,0xffffff81,0xffffffff};
    for(unsigned trial=0;trial<sizeof requests/sizeof *requests;++trial)for(unsigned scenario:{1u,6u}){
        seed(*native,trial+24000,scenario);native->gpr[5]._u32[0]=requests[trial];
        if(scenario==1){word(lane,0x1000);word(lane+8,0xfffff000);word(lane+12,0x1400);word(lane+24,0xfffff000);}
        memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,trial+24000);
    }
    for(unsigned scenario:{5u,7u}){
        seed(*native,25000+scenario,scenario);failures_left=32;
        memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,25000+scenario);
    }
    // Independent format/allocation assertions use naturally aligned frames;
    // the strict tests above also cover arbitrary selector biases.
    for(unsigned trial=0;trial<128;++trial)for(unsigned scenario:{0u,1u,2u,3u,5u,7u}){
        seed(*native,trial+22000,scenario);const uint32_t result=native->gpr[6]._u32[0],requested=native->gpr[5]._u32[0],flags=native->gpr[4]._u32[0];
        memcpy(reference.get(),native.get(),sizeof *native);reference->ls=reference->ls_store;compare(*native,*reference,trial+22000);
        const uint32_t address=scenario==0?(flags&~3u):scenario==2?0x200000:scenario==5 || scenario==7?0x300200:0x200400;
        const uint32_t gpu=scenario==0?0xffffffff:scenario==5 || scenario==7?0xc200:scenario==2?0x8000:0x8400;
        const uint32_t sync=scenario==0?(flags&1):scenario==5 || scenario==7?0x6391abc7:0x7f318bad;
        assert(word(native->ls+result)==address && word(native->ls+result+4)==gpu && word(native->ls+result+8)==sync);
        assert(word(native->ls+result+12)==address+(scenario==0?requested:128*(1+(requested-1)/128)));
        assert(native->gpr[3]._u32[0]==1);
    }
    for(unsigned trial=0;trial<7;++trial){
        seed(*native,trial+23000,1);switch(trial){case 0:native->ls=native->ls_store+16;break;case 1:native->image_id=40;break;case 2:native->policy_mode=1;break;case 3:native->resident_ovl=2;break;case 4:native->int_enable=1;break;case 5:native->irq_saved=1;break;case 6:native->irq_frame=reinterpret_cast<void*>(uintptr_t(1));break;}
        memcpy(reference.get(),native.get(),sizeof *native);GeometryRing plan;assert(!read_geometry_ring(*native,plan));assert(!memcmp(native.get(),reference.get(),sizeof *native));
    }
    for(unsigned pc:{0x37c0,0x3800,0x38d0,0x3930,0x3940,0x3948,0x39e8,0x3a18,0x3a24,0x3a98,0x3aec,0x3afc,0x3b18,0x3b28,0x3b58,0x3b88,0x3ba0,0x3bdc,0x3c78,0x3cc4,0x3cd0,0x3cf8,0x3d00,0x3d18,0x3d34,0x3d54,0x3dd0,0x3e04,0x3e68,0x3f10,0x3f40,0x3fa4,0x400c,0x4064,0x4078,0x40ac,0x40b4}){
        assert(visited[pc]);printf("ring block %X entries %u\n",pc,visited[pc]);
    }
    printf("Ring allocator %u strict full-state/ordered-I/O cases PASS;production routing/live/performance OPEN\n",checks);
}

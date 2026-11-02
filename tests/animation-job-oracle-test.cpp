#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_animation_host_quaternions.h"
#include "zlib.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

struct Transfer { uint32_t kind,lsa,ea,size;std::vector<uint8_t> data; };
static std::vector<Transfer> transfers;
static size_t transfer_index=0;
static std::string current_case;
static uint64_t jobs=0,total_transfers=0,quaternion_calls=0,nonempty_calls=0;
static std::map<unsigned,uint64_t> quaternion_counts;
static std::filesystem::path output_directory;
static bool dump_started=false,dump_finished=false;
static bool probe_enabled=true;
static std::array<buzz::AnimationHostQuaternion8,32> pending_quaternions;
static std::vector<uint16_t> pending_indices;
static unsigned pending_base=0;
static uint64_t compared_quaternions=0;
static uint64_t replaced_quaternion_calls=0;
static bool replace_quaternions=false;
static bool corrupt_quaternions=false,corruption_injected=false;
static uint64_t numeric_cases=0,numeric_samples=0;
static std::array<buzz::AnimationHostQuaternion8,32> pending_static_quaternions;
static std::vector<uint16_t> pending_static_indices;
static unsigned pending_static_base=0;
static uint64_t static_calls=0,static_nonempty=0,compared_static=0,replaced_static=0,numeric_static=0;
static void check_numeric_quaternions(const spu_context*);
static void animation_original_probe(unsigned,spu_context*);
static bool animation_native_quaternion_override(spu_context*);
static bool animation_native_static_quaternion_override(spu_context*);
static void drain(spu_context*,unsigned);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c,uint32_t stop){drain(c,stop);}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_stop(spu_context* c){fprintf(stderr,"unexpected stop %s pc=%X\n",current_case.c_str(),c->pc);abort();}
void spu_halt(spu_context* c){fprintf(stderr,"unexpected halt %s pc=%X\n",current_case.c_str(),c->pc);abort();}
void spu_wrch(spu_context* c,uint32_t channel,u128 value)
{
    const auto v=value._u32[0];
    switch(channel) {
    case MFC_LSA:c->mfc_lsa=v;return;
    case MFC_EAH:c->mfc_eah=v;return;
    case MFC_EAL:c->mfc_eal=v;return;
    case MFC_Size:c->mfc_size=v&0xffff;return;
    case MFC_TagID:c->mfc_tag=v&31;return;
    case MFC_WrTagMask:c->mfc_tag_mask=v;return;
    case MFC_WrTagUpdate:return;
    case MFC_Cmd: {
        const unsigned command=v&0xff,kind=(command&~3u)==0x40?1:(command&~3u)==0x20?2:0;
        assert(kind&&c->mfc_eah==0&&transfer_index<transfers.size());
        const auto& expected=transfers[transfer_index++];
        const unsigned lsa=c->mfc_lsa&SPU_LS_MASK;
        if(expected.kind!=kind||expected.lsa!=lsa||expected.ea!=c->mfc_eal||expected.size!=c->mfc_size) {
            fprintf(stderr,"transfer mismatch %s index=%zu actual=%u,%X,%X,%u expected=%u,%X,%X,%u\n",current_case.c_str(),transfer_index-1,kind,lsa,c->mfc_eal,c->mfc_size,expected.kind,expected.lsa,expected.ea,expected.size);abort();
        }
        assert(uint64_t(lsa)+expected.size<=SPU_LS_SIZE);
        if(kind==1)memcpy(c->ls+lsa,expected.data.data(),expected.size);
        else if(memcmp(c->ls+lsa,expected.data.data(),expected.size)) {
            fprintf(stderr,"published bytes mismatch %s transfer=%zu\n",current_case.c_str(),transfer_index-1);abort();
        }
        c->mfc_tag_status|=1u<<c->mfc_tag;return;
    }
    case SPU_WrOutMbox:spu_channel_write(&c->ch_out_mbox,v);return;
    case SPU_WrOutIntrMbox:spu_channel_write(&c->ch_out_intr_mbox,v);return;
    default:fprintf(stderr,"unexpected write channel=%u %s\n",channel,current_case.c_str());abort();
    }
}
u128 spu_rdch(spu_context* c,uint32_t channel)
{
    switch(channel) {
    case MFC_RdTagStat:return spu_il(c->mfc_tag_status&c->mfc_tag_mask);
    case SPU_RdMachStat:return spu_il(c->int_enable&1);
    case SPU_RdInMbox:assert(spu_channel_has_data(&c->ch_in_mbox));return spu_il(spu_channel_read(&c->ch_in_mbox));
    default:fprintf(stderr,"unexpected read channel=%u %s\n",channel,current_case.c_str());abort();
    }
}
uint32_t spu_rchcnt(spu_context* c,uint32_t channel){assert(channel==SPU_RdInMbox);return c->ch_in_mbox.count;}
}
#include "../analysis/native-geometry/phase4/job-oracle-v3/reference.inc"
extern "C" void spu_indirect_branch(spu_context* c)
{
    auto next=animation_original_lookup(c->pc&SPU_LS_MASK);
    if(!next){fprintf(stderr,"missing original PC=%X %s\n",c->pc,current_case.c_str());abort();}next(c);
}
static void drain(spu_context* c,unsigned stop)
{
    unsigned steps=0;
    while(g_spu_trampoline_fn) {
        assert(++steps<1048576);
        if((c->pc&SPU_LS_MASK)==stop){g_spu_trampoline_fn=nullptr;break;}
        auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);
    }
    if(c->pc!=stop){fprintf(stderr,"original return mismatch pc=%X stop=%X %s\n",c->pc,stop,current_case.c_str());abort();}
}
static void dump(const char* name,const void* data,size_t bytes)
{
    auto* file=fopen((output_directory/name).string().c_str(),"wb");assert(file);
    assert(fwrite(data,1,bytes,file)==bytes);assert(!fclose(file));
}
static void animation_original_probe(unsigned pc,spu_context* c)
{
    if(!probe_enabled)return;
    if(pc==0x7490) {
        ++static_calls;const unsigned count=c->gpr[5]._u32[0];assert(count<=256);
        if(static_nonempty==0&&count)printf("first static quaternion producer count=%u destination=%X keys=%X indices=%X permutation=%X\n",count,c->gpr[3]._u32[0],c->gpr[4]._u32[0],c->gpr[6]._u32[0],c->gpr[7]._u32[0]);
        static_nonempty+=count!=0;pending_static_base=c->gpr[3]._u32[0];pending_static_indices.clear();
        const unsigned at=c->gpr[4]._u32[0];assert(at<=SPU_LS_SIZE);
        assert(buzz::decode_animation_host_static_quaternions({c->ls+at,SPU_LS_SIZE-at},count,pending_static_quaternions.data()));
        for(unsigned i=0;i<count;++i){const unsigned id=c->gpr[6]._u32[0]+2*i;assert(id+2<=SPU_LS_SIZE);pending_static_indices.push_back(uint16_t(c->ls[id])<<8|c->ls[id+1]);}
        return;
    }
    if(pc==0x7788) {
        for(unsigned i=0;!replace_quaternions&&i<pending_static_indices.size();++i) {
            alignas(32) uint32_t values[4][8];
            for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_static_quaternions[i/8].component[component]);
            const unsigned at=pending_static_base+pending_static_indices[i]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){const auto* p=c->ls+at+component*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(values[component][i%8]!=expected){fprintf(stderr,"static quaternion mismatch %s track=%u joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),i,pending_static_indices[i],component,expected,values[component][i%8]);abort();}}
            ++compared_static;
        }
        return;
    }
    if(pc==0x6950) {
        ++quaternion_calls;const unsigned count=c->gpr[17]._u32[0];++quaternion_counts[count];
        if(count)++nonempty_calls;
        assert(count<=256);pending_indices.clear();pending_base=c->gpr[18]._u32[0];
        auto view=[&](unsigned at){assert(at<=SPU_LS_SIZE);return buzz::AnimationByteView{c->ls+at,SPU_LS_SIZE-at};};
        const unsigned bit=c->gpr[3]._u32[0];
        buzz::AnimationQuaternionTracks recipe{view(bit/8),view(c->gpr[15]._u32[0]),view(c->gpr[16]._u32[0]),view(c->gpr[14]._u32[0]),bit%8,c->gpr[20]._u32[0],count,c->gpr[12]._u32[0],c->gpr[13]._u32[0]};
        if(!buzz::sample_animation_host_quaternions(recipe,pending_quaternions.data())){fprintf(stderr,"native quaternion recipe rejected %s frames=%u frame=%u count=%u\n",current_case.c_str(),recipe.interior_frames,recipe.frame,count);abort();}
        for(unsigned i=0;i<count;++i){const auto at=c->gpr[19]._u32[0]+2*i;assert(at+2<=SPU_LS_SIZE);pending_indices.push_back(uint16_t(c->ls[at])<<8|c->ls[at+1]);}
        if(count&&!dump_started) {
            dump_started=true;dump("quaternion-entry-arguments.bin",c->gpr+3,18*sizeof(u128));
            dump("quaternion-entry-ls.bin",c->ls,SPU_LS_SIZE);
            check_numeric_quaternions(c);
            printf("first quaternion producer: %s count=%u\n",current_case.c_str(),count);
            for(unsigned r=3;r<=20;++r)printf("arg%u %08X %08X %08X %08X\n",r,c->gpr[r]._u32[0],c->gpr[r]._u32[1],c->gpr[r]._u32[2],c->gpr[r]._u32[3]);
            // Observe the original prelude on a private copy. This is an
            // analysis snapshot, never the native representation or a gate.
            auto shadow=std::make_unique<spu_context>();memcpy(shadow.get(),c,sizeof(*c));shadow->ls=shadow->ls_store;
            auto saved=g_spu_trampoline_fn;probe_enabled=false;g_spu_trampoline_fn=nullptr;
            buzz_job_abe1_spu_func_00006950(shadow.get());
            assert(shadow->pc==0x6db8);
            for(unsigned r:{2,22,24,25,26,27,28,29,30,31,32,54,57,60,61,77,87,98})
                printf("loop%u %08X %08X %08X %08X\n",r,shadow->gpr[r]._u32[0],shadow->gpr[r]._u32[1],shadow->gpr[r]._u32[2],shadow->gpr[r]._u32[3]);
            g_spu_trampoline_fn=saved;probe_enabled=true;
        }
    } else {
        for(unsigned i=0;!replace_quaternions&&i<pending_indices.size();++i) {
            alignas(32) uint32_t values[4][8];
            for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_quaternions[i/8].component[component]);
            const unsigned at=pending_base+pending_indices[i]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){const auto* p=c->ls+at+component*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(values[component][i%8]!=expected){fprintf(stderr,"native quaternion mismatch %s track=%u joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),i,pending_indices[i],component,expected,values[component][i%8]);abort();}}
            ++compared_quaternions;
        }
        if(dump_started&&!dump_finished){dump_finished=true;dump("quaternion-exit-ls.bin",c->ls,SPU_LS_SIZE);}
    }
}
static bool animation_native_quaternion_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x6950,c);
    for(unsigned first=0;first<pending_indices.size();first+=8) {
        alignas(32) uint32_t values[4][8];
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_quaternions[first/8].component[component]);
        const unsigned count=std::min(8u,unsigned(pending_indices.size())-first);
        for(unsigned lane=0;lane<count;++lane) {
            const unsigned at=pending_base+pending_indices[first+lane]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){auto* p=c->ls+at+component*4;const unsigned word=values[component][lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;}
        }
    }
    ++replaced_quaternion_calls;animation_original_probe(0x7408,c);
    if(corrupt_quaternions&&!corruption_injected&&!pending_indices.empty()) {
        c->ls[pending_base+pending_indices[0]*48]^=0x80;corruption_injected=true;
    }
    // Only the original test caller's continuation is exposed. No volatile
    // GPRs, prefetch state, scratch or saved SPU frames are reconstructed.
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_static_quaternion_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x7490,c);
    for(unsigned first=0;first<pending_static_indices.size();first+=8) {
        alignas(32) uint32_t values[4][8];
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_static_quaternions[first/8].component[component]);
        const unsigned count=std::min(8u,unsigned(pending_static_indices.size())-first);
        for(unsigned lane=0;lane<count;++lane) {
            const unsigned at=pending_static_base+pending_static_indices[first+lane]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){auto* p=c->ls+at+component*4;const unsigned word=values[component][lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;}
        }
    }
    ++replaced_static;c->pc=c->gpr[0]._u32[0];return true;
}
static buzz::AnimationByteView guarded_view(const uint8_t* data,unsigned bytes,unsigned slot)
{
#ifdef _WIN32
    static uint8_t* pages[4]{};
    if(!pages[slot]){pages[slot]=static_cast<uint8_t*>(VirtualAlloc(nullptr,12288,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(pages[slot]);DWORD old;assert(VirtualProtect(pages[slot]+8192,4096,PAGE_NOACCESS,&old));}
    assert(bytes<=8192);DWORD old;assert(VirtualProtect(pages[slot],8192,PAGE_READWRITE,&old));
    auto* destination=pages[slot]+8192-bytes;if(bytes)memcpy(destination,data,bytes);
    assert(VirtualProtect(pages[slot],8192,PAGE_READONLY,&old));return {destination,bytes};
#else
    return {data,bytes};
#endif
}
static void check_numeric_quaternions(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned bits_at=0x1c000,ids_at=0x1f000,first_at=0x20000,last_at=0x21000,keys_at=0x22000,dst=0x24000;
    constexpr unsigned widths[]={0,1,2,7,15,31,32,33,63,64,65,79,95,96,97,127,128};
    constexpr uint32_t fractions[]={0,0x80000000,0x3e800000,0x3f000000,0x3f7fbe77,1};
    uint32_t random=0xc451b6f2;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    auto pack=[&](unsigned salt) {
        const unsigned a=salt%5==0?16384:8000+next()%16000,b=salt%5==0?16384:8000+next()%16000,d=salt%5==0?16384:8000+next()%16000;
        return uint64_t(a)<<32|uint64_t(b)<<17|uint64_t(d)<<2|(salt%4);
    };
    auto put=[&](unsigned at,uint64_t value){for(int i=5;i>=0;--i){c->ls[at+i]=uint8_t(value);value>>=8;}};
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        memset(c->ls+bits_at,0,0x9000);
        const unsigned width=widths[test%std::size(widths)],count=1+test%8,first_bit=test%8;
        const unsigned frame=(test/17)%(width+1),key_bytes=(first_bit+count*width+7)/8;
        unsigned keys=0;
        for(unsigned track=0;track<count;++track) {
            const uint64_t endpoint=pack(test+track);put(first_at+track*6,endpoint);put(last_at+track*6,test%3?pack(test+track+3):endpoint);
            c->ls[ids_at+track*2]=0;c->ls[ids_at+track*2+1]=uint8_t(count-1-track);
            for(unsigned i=0;i<width;++i) if((test%7==0)||(test%7!=1&&next()%5==0)) {
                const unsigned bit=first_bit+track*width+i;c->ls[bits_at+bit/8]|=0x80u>>(bit%8);
                put(keys_at+keys++*6,pack(test+track+i));
            }
        }
        // Original lookahead stores target spare joints, outside logical data.
        for(unsigned i=count;i<count+8;++i)c->ls[ids_at+i*2+1]=uint8_t(i);
        buzz::AnimationQuaternionTracks recipe{
            guarded_view(c->ls+bits_at,key_bytes,0),guarded_view(c->ls+first_at,count*6,1),
            guarded_view(c->ls+last_at,count*6,2),guarded_view(c->ls+keys_at,keys*6,3),
            first_bit,width,count,frame,fractions[test%std::size(fractions)]};
        buzz::AnimationHostQuaternion8 actual;
        assert(buzz::sample_animation_host_quaternions(recipe,&actual));
        auto top=[&](unsigned n){return n==32?~0u:n?~0u<<(32-n):0u;};
        for(unsigned word=0;word<4;++word) {
            const unsigned valid=width>word*32?std::min(32u,width-word*32):0;
            const unsigned prefix=frame>word*32?std::min(32u,frame-word*32):0;
            c->gpr[4+word]=spu_il(top(valid));c->gpr[8+word]=spu_il(top(prefix));
        }
        for(unsigned lane=0;lane<4;++lane) {
            c->gpr[3]._u32[lane]=bits_at*8+first_bit+lane*width;
            c->gpr[15]._u32[lane]=first_at+lane*6;c->gpr[16]._u32[lane]=last_at+lane*6;
        }
        c->gpr[0]=spu_link(0x1234);c->gpr[12]=spu_il(frame);c->gpr[13]=spu_il(recipe.fraction);
        c->gpr[14]=spu_il(keys_at);c->gpr[17]=spu_il(count);c->gpr[18]=spu_il(dst);c->gpr[19]=spu_il(ids_at);c->gpr[20]=spu_il(width);
        current_case=saved_case+"/numeric-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00006950(c.get());drain(c.get(),0x1234);
        alignas(32) uint32_t values[4][8];for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),actual.component[component]);
        for(unsigned track=0;track<count;++track)for(unsigned component=0;component<4;++component) {
            const auto* at=c->ls+dst+(count-1-track)*48+component*4;
            const unsigned expected=uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|at[3];
            if(values[component][track]!=expected){fprintf(stderr,"numeric quaternion mismatch test=%u width=%u frame=%u count=%u track=%u component=%u expected=%08X actual=%08X\n",test,width,frame,count,track,component,expected,values[component][track]);abort();}
        }
        assert(buzz::decode_animation_host_static_quaternions(recipe.first,count,&actual));
        c->gpr[0]=spu_link(0x1238);c->gpr[3]=spu_il(dst);c->gpr[4]=spu_il(first_at);c->gpr[5]=spu_il(count);c->gpr[6]=spu_il(ids_at);c->gpr[7]=spu_il(0xa810);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00007490(c.get());drain(c.get(),0x1238);
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),actual.component[component]);
        for(unsigned track=0;track<count;++track)for(unsigned component=0;component<4;++component) {
            const auto* at=c->ls+dst+(count-1-track)*48+component*4;
            const unsigned expected=uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|at[3];
            if(values[component][track]!=expected){fprintf(stderr,"numeric static quaternion mismatch test=%u track=%u component=%u expected=%08X actual=%08X key=%02X%02X%02X%02X%02X%02X\n",test,track,component,expected,values[component][track],c->ls[first_at+track*6],c->ls[first_at+track*6+1],c->ls[first_at+track*6+2],c->ls[first_at+track*6+3],c->ls[first_at+track*6+4],c->ls[first_at+track*6+5]);abort();}
        }
        numeric_static+=count;
        ++numeric_cases;numeric_samples+=count;
    }
    const uint8_t bit=0x80,key[]={0x40,0,0x80,1,0,3};
    buzz::AnimationQuaternionTracks good{{&bit,1},{key,6},{key,6},{key,6},0,1,1,0,0};
    buzz::AnimationHostQuaternion8 result;
    for(unsigned rejection=0;rejection<6;++rejection) {
        auto bad=good;
        switch(rejection){case 0:bad.first_bit=8;break;case 1:bad.frame=3;break;case 2:bad.key_bits.bytes=0;break;case 3:bad.first.bytes=5;break;case 4:bad.last.bytes=5;break;case 5:bad.interior.bytes=5;break;}
        assert(!buzz::sample_animation_host_quaternions(bad,&result));
    }
    assert(buzz::sample_animation_host_quaternions({},nullptr));
    assert(!buzz::decode_animation_host_static_quaternions({key,5},1,&result));
    assert(buzz::decode_animation_host_static_quaternions({},0,nullptr));
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
int main(int argc,char** argv)
{
    assert(argc==4);output_directory=argv[2];assert(std::filesystem::is_directory(output_directory));
    assert(std::string(argv[3])=="Compare"||std::string(argv[3])=="Replace"||std::string(argv[3])=="Corrupt");
    corrupt_quaternions=std::string(argv[3])=="Corrupt";replace_quaternions=std::string(argv[3])!="Compare";
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto c=std::make_unique<spu_context>();std::vector<std::filesystem::path> files;
    for(auto& file:std::filesystem::directory_iterator(argv[1]))if(file.path().extension()==".gz")files.push_back(file.path());
    std::sort(files.begin(),files.end());
    std::array<uint8_t,2048+SPU_LS_SIZE> skipped;
    for(auto& path:files) {
        current_case=path.filename().string();auto* file=gzopen(path.string().c_str(),"rb");assert(file);
        auto read=[&](void* data,unsigned n){assert(gzread(file,data,n)==int(n));};
        uint32_t header[6];read(header,sizeof(header));assert(header[0]==0x434a5a42&&header[1]==1&&header[2]==40&&header[5]==SPU_LS_SIZE);
        memset(c.get(),0,sizeof(*c));c->ls=c->ls_store;c->image_id=40;c->host_depth=1;
        read(c->gpr,sizeof(c->gpr));read(c->ls,SPU_LS_SIZE);
        uint32_t n;read(&n,4);transfers.clear();transfer_index=0;
        for(unsigned i=0;i<n;++i){uint32_t h[4];read(h,16);Transfer t{h[0],h[1],h[2],h[3],{}};assert((t.kind==1||t.kind==2)&&uint64_t(t.lsa)+t.size<=SPU_LS_SIZE);t.data.resize(t.size);read(t.data.data(),t.size);transfers.push_back(std::move(t));}
        read(skipped.data(),unsigned(skipped.size()));uint32_t expected[7];read(expected,sizeof(expected));assert(gzread(file,skipped.data(),1)==0);assert(gzclose(file)==Z_OK);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00000010(c.get());drain(c.get(),expected[0]);
        assert(transfer_index==transfers.size()&&c->host_depth==1&&c->image_id==40&&c->ls==c->ls_store&&!g_spu_trampoline_fn);
        const uint32_t exit[]={c->pc,c->status,c->stop_code,unsigned(spu_channel_has_data(&c->ch_out_mbox)),c->ch_out_mbox.value,unsigned(spu_channel_has_data(&c->ch_out_intr_mbox)),c->ch_out_intr_mbox.value};
        assert(!memcmp(exit,expected,sizeof(exit)));++jobs;total_transfers+=n;
    }
    assert(jobs==17714&&total_transfers==115162&&dump_finished);
    printf("PASS animation whole-job oracle mode=%s: %llu exact jobs / %llu ordered transfers; quaternion calls=%llu nonempty=%llu\n",argv[3],(unsigned long long)jobs,(unsigned long long)total_transfers,(unsigned long long)quaternion_calls,(unsigned long long)nonempty_calls);
    printf("PASS native host quaternion samples=%llu\n",(unsigned long long)compared_quaternions);
    printf("PASS native quaternion producer replacements=%llu\n",(unsigned long long)replaced_quaternion_calls);
    printf("PASS guarded quaternion numeric batches=%llu samples=%llu\n",(unsigned long long)numeric_cases,(unsigned long long)numeric_samples);
    printf("PASS static quaternion calls=%llu nonempty=%llu compared=%llu replaced=%llu numeric=%llu\n",(unsigned long long)static_calls,(unsigned long long)static_nonempty,(unsigned long long)compared_static,(unsigned long long)replaced_static,(unsigned long long)numeric_static);
    puts("PASS six invalid stream recipes rejected; empty track set reads/writes no data");
    for(auto [count,n]:quaternion_counts)printf("quaternion count=%u calls=%llu\n",count,(unsigned long long)n);
}

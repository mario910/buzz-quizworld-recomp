// Diagnostic only: preserve the original calls/results and inspect transitions.
#include "video_probe.h"
#include <cstdio>
#include <mutex>
#include <chrono>
static void codec_probe(uint32_t pc,uint32_t object,uint32_t result,uint32_t arg4) {
    struct Entry {uint32_t pc,object,last;unsigned calls,prints;std::chrono::steady_clock::time_point time;};
    static Entry entries[64];static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    Entry* entry=nullptr;
    for(auto& item:entries)if(item.pc==pc && item.object==object){entry=&item;break;}
    if(!entry)for(auto& item:entries)if(!item.pc){entry=&item;item.pc=pc;item.object=object;break;}
    if(!entry)return;
    ++entry->calls;auto now=std::chrono::steady_clock::now();
    bool report=entry->prints<20 && (entry->calls<=3 || entry->last!=result || now-entry->time>std::chrono::seconds(5));
    entry->last=result;
    if(!report)return;
    ++entry->prints;entry->time=now;
    std::fprintf(stderr,"[video-codec] pc=%08X object=%08X arg4=%08X result=%08X calls=%u",pc,object,arg4,result,entry->calls);
    uint32_t handle=pc==0x49229c?result:object;
    if(handle>=0x10000 && handle<0xbffff000 && pc>=0x492000 && pc!=0x4ab3e8)
        std::fprintf(stderr," active=%u mpv=%08X sj=%08X state514=%u n518=%u n51c=%u n520=%u",
            vm_read32(handle),vm_read32(handle+0x58),vm_read32(handle+0x500),vm_read32(handle+0x514),
            vm_read32(handle+0x518),vm_read32(handle+0x51c),vm_read32(handle+0x520));
    if(pc==0x4ab3e8 && object>=0x10000 && object<0xbfffd000)
        std::fprintf(stderr," raw50=%u raw1ffc=%08X",vm_read32(object+0x50),vm_read32(object+0x1ffc));
    if(pc==0x48eb0c && object>=0x10000 && object<0xbfffff00)
        std::fprintf(stderr," sj-active=%u chunks1=%u chunks0=%u head=%08X tail=%08X",
            vm_read32(object+4),vm_read32(object+0xc),vm_read32(object+0x10),vm_read32(object+0x14),vm_read32(object+0x18));
    if((pc==0x30617c || pc==0x484f94) && object>=0x10000 && object<0xbffff000)
        std::fprintf(stderr," tick-enabled=%u tick-count=%u workers=%08X,%08X,%08X",
            vm_read32(object+0x1b4),vm_read32(object+0x14),vm_read32(object+0x38),vm_read32(object+0x3c),vm_read32(object+0x40));
    if(pc>=0x486000 && pc<0x486100 && object>=0x10000 && object<0xbfffff00) {
        uint32_t desc=vm_read32(object+0x28);
        std::fprintf(stderr," worker-state=%u flags=%X rounds=%u callback-opd=%08X callback-code=%08X callback-arg=%08X",
            vm_read32(object+0x20),vm_read32(object+0x24),vm_read32(object+0x1c),desc,
            desc>=0x10000 && desc<0xbffffff8?vm_read32(desc):0,vm_read32(object+0x2c));
        uint32_t wait=vm_read32(object+0x30),wake=vm_read32(object+0x38);
        std::fprintf(stderr," wait-code=%08X wait-arg=%08X wake-code=%08X wake-arg=%08X",
            wait>=0x10000 && wait<0xbffffff8?vm_read32(wait):0,vm_read32(object+0x34),
            wake>=0x10000 && wake<0xbffffff8?vm_read32(wake):0,vm_read32(object+0x3c));
    }
    std::fputc('\n',stderr);
}
void buzz_video_probe(uint32_t pc,uint32_t object,uint32_t result,uint32_t arg4) {
    if(pc>=0x480000 || pc==0x30617c){codec_probe(pc,object,result,arg4);return;}
    // IO methods receive the embedded member at player+0x78.
    if(pc==0x305550 || pc==0x305470)object-=0x78;
    if(object<0x10000 || object>0xbffff800)return;
    struct Snapshot {uint32_t player,io,decoder,width,height,frame,flags;};
    struct Slot {uint32_t object;Snapshot state;unsigned prints;std::chrono::steady_clock::time_point time;};
    static Slot slots[16];static std::mutex mutex;
    std::lock_guard<std::mutex> guard(mutex);
    Slot* slot=nullptr;
    for(auto& item:slots)if(item.object==object){slot=&item;break;}
    if(!slot)for(auto& item:slots)if(!item.object){slot=&item;item.object=object;break;}
    if(!slot)return;
    Snapshot current={vm_read32(object+0x76c),vm_read32(object+0x8c),
        vm_read32(object+0x66c),vm_read32(object+0xa8),vm_read32(object+0xac),
        vm_read32(object+0x764),vm_read8(object+0x769)};
    auto now=std::chrono::steady_clock::now();
    bool stage_changed=current.player!=slot->state.player &&
        !(current.player>=3 && current.player<=4 && slot->state.player>=3 && slot->state.player<=4);
    bool changed=stage_changed || current.io!=slot->state.io ||
        current.decoder!=slot->state.decoder || current.width!=slot->state.width ||
        current.height!=slot->state.height || current.flags!=slot->state.flags;
    if(slot->prints<128 && (!slot->prints || changed || now-slot->time>std::chrono::seconds(3))) {
        ++slot->prints;slot->time=now;
        std::fprintf(stderr,"[video-state] pc=%08X object=%08X result=%08X player=%u io=%u decoder=%08X dims=%ux%u frame=%u flags=%X\n",
            pc,object,result,current.player,current.io,current.decoder,current.width,current.height,current.frame,current.flags);
        uint32_t sj=vm_read32(object+0x78);
        if(sj>=0x10000 && sj<0xbfffff00) {
            uint32_t vt=vm_read32(sj);
            if(vt>=0x10000 && vt<0xbfffff00) {
                uint32_t desc=vm_read32(vt+0x24);
                if(desc>=0x10000 && desc<0xbffffff8)
                    std::fprintf(stderr,"[video-input] sj=%08X available-opd=%08X available-code=%08X\n",sj,desc,vm_read32(desc));
            }
        }
    }
    if(pc==0x305a08 && result==1 && current.frame>=30 && current.frame<0xffff0000) {
        static bool captured=false;
        if(!captured) {
            captured=true;
            uint32_t job=vm_read32(object+0x718);
            std::fprintf(stderr,"[video-converted] player=%08X frame=%u job=%08X\n",object,current.frame,job);
            if(job>=0x10000 && job<=0xffffff80u) {
                std::fprintf(stderr,"[video-converted] frame=%u job=%08X words:",current.frame,job);
                for(unsigned o=0;o<128;o+=4)std::fprintf(stderr," %08X",vm_read32(job+o));
                std::fputc('\n',stderr);
                uint32_t pixels=vm_read32(job+0x50),w=vm_read32(job+0x54),h=vm_read32(job+0x58),pitch=vm_read32(job+0x5c);
                if(pixels>=0x10000 && w>0 && w<=4096 && h>0 && h<=4096 && pitch>=w*4 && pitch<=65536 &&
                    (uint64_t)pixels+(uint64_t)pitch*h<=0x100000000ull) {
                    uint64_t hash=1469598103934665603ull;unsigned nonzero=0,rgb=0;
                    // CRI format 0x11 writes RGBA bytes, hence BE RGB is bits 31..8.
                    for(unsigned o=0;o<pitch*h;o+=4){uint32_t v=vm_read32(pixels+o);hash=(hash^v)*1099511628211ull;nonzero+=v!=0;rgb+=(v&0xffffff00u)!=0;}
                    std::fprintf(stderr,"[video-converted] pixels=%08X dims=%ux%u pitch=%u nonzero=%u rgb=%u hash=%016llX first=%08X\n",pixels,w,h,pitch,nonzero,rgb,(unsigned long long)hash,vm_read32(pixels));
                    if(const char* trigger=std::getenv("BUZZ_VIDEO_CAPTURE_TRIGGER")) {
                        char path[1024];int n=std::snprintf(path,sizeof(path),"%s.pixels",trigger);
                        if(n>0 && (unsigned)n<sizeof(path)) {
                            FILE* f=std::fopen(path,"wb");if(f){std::fwrite(vm_base+pixels,1,(size_t)pitch*h,f);std::fclose(f);}
                        }
                        uint32_t y=vm_read32(job+0x10),yw=vm_read32(job+0x14),yh=vm_read32(job+0x18),yp=vm_read32(job+0x1c);
                        if(y>=0x10000 && yw && yw<=4096 && yh && yh<=4096 && yp>=yw && yp<=65536 && (uint64_t)y+(uint64_t)yp*yh<=0x100000000ull) {
                            unsigned lo=255,hi=0;
                            for(unsigned o=0;o<yp*yh;++o){unsigned v=vm_read8(y+o);if(v<lo)lo=v;if(v>hi)hi=v;}
                            std::fprintf(stderr,"[video-converted] Y=%08X dims=%ux%u pitch=%u range=%u..%u\n",y,yw,yh,yp,lo,hi);
                            n=std::snprintf(path,sizeof(path),"%s.y",trigger);
                            if(n>0 && (unsigned)n<sizeof(path)){FILE* f=std::fopen(path,"wb");if(f){std::fwrite(vm_base+y,1,(size_t)yp*yh,f);std::fclose(f);}}
                        }
                    }
                }
            }
            if(const char* trigger=std::getenv("BUZZ_VIDEO_CAPTURE_TRIGGER")) {
                // The renderer expects a monotonically increasing numeric request.
                FILE* f=std::fopen(trigger,"wb");if(f){std::fputs("1\n",f);std::fclose(f);}
            }
        }
    }
    slot->state=current;
}

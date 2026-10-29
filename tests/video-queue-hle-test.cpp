// Exercise the production title HLE, with traps around the SDK boundary.
#include "../port/video_queue.cpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <atomic>
extern "C" {uint8_t* vm_base=nullptr;}
static std::mutex line;
static thread_local bool held=false;
static unsigned invalidations,signals,fallbacks;
static void (*registered)(ppu_context*);
extern "C" void ps3_hle_register_ctx(uint32_t nid,const char*,void (*fn)(ppu_context*)) {assert(nid==0x011ee38b);registered=fn;}
extern "C" int32_t _cellSpursLFQueueInitialize(uint64_t a,uint64_t b,uint64_t c,uint32_t d,uint32_t e,uint32_t f) {
    assert(a==0x5000 && b==0x2000 && c==0x2080 && d==16 && e==3 && f==2);++fallbacks;return -123;
}
extern "C" void spu_lockline_lock(void) {assert(!held);line.lock();held=true;}
extern "C" void spu_lockline_unlock(void) {assert(held);held=false;line.unlock();}
extern "C" int spu_coh_is_reserved(uint32_t q) {assert(held && q==0x2000);return 1;}
extern "C" void spu_coh_notify_write(uint32_t q) {assert(held && q==0x2000);++invalidations;}
extern "C" void spu_taskset_signal_task(uint32_t owner,uint32_t task) {assert(!held && owner==0x5000 && task==0);++signals;}
static void init(ppu_context& c,uint32_t lr) {
    c={};c.lr=lr;c.gpr[3]=0x5000;c.gpr[4]=0x2000;c.gpr[5]=0x2080;c.gpr[6]=16;c.gpr[7]=3;c.gpr[8]=2;
    registered(&c);
}
static bool push(ppu_context& c,uint32_t lr,uint32_t blocking=0) {
    c={};c.lr=lr;c.gpr[3]=0x2000;c.gpr[4]=0x4000;c.gpr[5]=blocking;return buzz_video_queue_push(&c);
}
int main() {
    vm_base=(uint8_t*)std::calloc(1,65536);assert(vm_base);buzz_register_video_queue();assert(registered);
    ppu_context c;init(c,0x111111);assert((int64_t)c.gpr[3]==-123 && fallbacks==1);
    init(c,0x490d84);assert(c.gpr[3]==0 && buzz_vq_r16(vm_base+0x200e)==0);
    for(unsigned n=0;n<16;++n)vm_base[0x4000+n]=(uint8_t)(n+1);
    assert(!push(c,0x111111) && c.gpr[3]==0x2000 && invalidations==0);
    buzz_vq_w16(vm_base+0x2030,1); // Actual shape of one registered task-0 waiter.
    assert(push(c,0x4906d4) && c.gpr[3]==0 && signals==1 && invalidations==1);
    assert(!std::memcmp(vm_base+0x2080,vm_base+0x4000,16));
    assert(push(c,0x4906d4) && c.gpr[3]==0 && signals==1);
    assert(push(c,0x490a30) && c.gpr[3]==0 && signals==1); // Shutdown sentinel follows the same queue protocol.
    assert(push(c,0x4906d4) && (uint32_t)c.gpr[3]==0x80410901u && invalidations==3);
    std::thread consumer([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        spu_lockline_lock();buzz_vq_w16(vm_base+0x2000,1);spu_lockline_unlock();
    });
    assert(push(c,0x4906d4,3) && c.gpr[3]==0);consumer.join();
    assert(invalidations==4 && signals==1 && buzz_vq_r16(vm_base+0x2008)==4);
    std::free(vm_base);vm_base=nullptr;
    std::puts("PASS: production HLE routing, legacy fallback, coherent publication, wake after unlock, full/try/blocking, shutdown enqueue");
}

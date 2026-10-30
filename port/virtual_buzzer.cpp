// Title-scoped cellUsbd adapter. The game's own LDD parses real USB reports.
// No game/menu state is patched. Descriptor and report layout are compatible
// with the Sony 054c:1000 Buzz receiver (four five-button handsets).
#include "ppu_recomp.h"
#include "../ps3recomp/runtime/ppu/ppu_tls_image.h"
#include "buzzer_protocol.h"
#include "buzzer_script.h"
#include "buzzer_input.h"
#include "../ps3recomp/runtime/memory/vm.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

extern "C" {
void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
extern vm_stack_alloc g_vm_stack_alloc;
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
#ifdef BUZZ_EXPERIMENTAL_VULKAN
void rsx_vulkan_set_key_handler(void (*)(uint32_t,int));
#define buzz_set_host_key_handler rsx_vulkan_set_key_handler
#else
void rsx_d3d12_set_key_handler(void (*)(uint32_t,int));
#define buzz_set_host_key_handler rsx_d3d12_set_key_handler
#endif
}

namespace {
constexpr uint32_t bad_param = 0x80110004, no_device = 0x8011000A;
constexpr uint32_t no_pipe = 0x8011000D;
constexpr uint32_t stack_size = 0x100000;
using buzz_usb::descriptors;
struct Transfer {
    uint32_t pipe, buffer, size, callback, arg;
    bool interrupt;
    uint8_t request[8];
    uint64_t due;
    unsigned receiver=0;
};
std::mutex state_mutex;
std::deque<Transfer> transfers;
std::array<uint8_t, 32> pipes{};
std::array<unsigned,32> pipe_receivers{};
uint32_t arena = 0, ldd = 0;
struct Receiver { uint32_t private_data=0; bool attached=false; };
std::array<Receiver,2> receivers{};
unsigned receiver_count=1;
std::atomic<bool> online{false}, attach_pending{false};
std::atomic<uint64_t> reports{0};
std::atomic<uint32_t> callback_pc{0};
std::unordered_map<uint32_t,void(*)(ppu_context*)> guest_functions;
using buzz_usb::ScriptEvent;
std::array<ScriptEvent,64> script{};
unsigned script_count=0;
uint64_t script_start=0;
const char* test_input_path=nullptr;
buzz_usb::TestMailbox test_mailbox;
uint64_t test_input_next_poll=0;

uint64_t now_ms() { return GetTickCount64(); }
void result(ppu_context* c, uint32_t v) { c->gpr[3] = (uint64_t)(int64_t)(int32_t)v; }

uint64_t callback(uint32_t opd, uint64_t a = 0, uint64_t b = 0, uint64_t d = 0)
{
    if (!opd) return 0;
    uint32_t code = vm_read32(opd), toc = vm_read32(opd+4);
    auto it = guest_functions.find(code);
    if (it == guest_functions.end()) {
        fprintf(stderr,"[buzzer] missing callback OPD=%08X code=%08X\n",opd,code);
        return (uint64_t)-1;
    }
    // Dedicated guest stack and TLS: do not alias the renderer's callback stack
    // or the main thread's allocator TLS. Initialized from the loaded ELF's PT_TLS.
    ppu_context c{};
    c.gpr[1] = arena + stack_size - 0x100;
    c.gpr[2] = toc;
    c.gpr[13] = arena + 0x2000 + 0x7000;
    c.gpr[3]=a; c.gpr[4]=b; c.gpr[5]=d;
    c.thread_id = 0x7FFF4255;
    c.cia = code;
    auto* previous = g_active_ctx;
    auto previous_trampoline = g_trampoline_fn;
    g_trampoline_fn = nullptr;
    g_active_ctx = &c;
    callback_pc=code;
    it->second(&c);
    while (g_trampoline_fn) {
        auto fn = g_trampoline_fn; g_trampoline_fn = nullptr; fn(&c);
    }
    g_active_ctx = previous;
    g_trampoline_fn = previous_trampoline;
    callback_pc=0;
    return c.gpr[3];
}

uint64_t input_buttons()
{
    const auto now=now_ms();
    // Explicit opt-in, reproducible input test; never enabled by the launcher.
    uint32_t pulses=buzz_usb::due_script_pulses(
        script.data(),script_count,(double)(now-script_start)/1000.0);
    if(test_input_path && now>=test_input_next_poll) {
        test_input_next_poll=now+25; // Diagnostic opt-in only; disabled in ordinary play.
        if(FILE* f=fopen(test_input_path,"rb")) {
            char command[96]{};const size_t n=fread(command,1,sizeof(command)-1,f);
            const bool complete=n<sizeof(command)-1 && !ferror(f);fclose(f);
            if(complete)pulses|=test_mailbox.consume(command);
        }
    }
    return buzz_input_poll(pulses);
}

uint32_t control_response(const Transfer& t)
{
    uint8_t data[256]{};
    uint32_t n=0;
    // Request u16 fields are native guest BE values at the cellUsbd boundary.
    const uint16_t value=(uint16_t)((t.request[2]<<8)|t.request[3]);
    if (t.request[1]==6) { // GET_DESCRIPTOR
        unsigned type=value>>8, index=value&255;
        if (type==3) {
            if (!index) { data[0]=4;data[1]=3;data[2]=9;data[3]=4;n=4; }
            else {
                const char* text=index==1 ? "Buzz USB Controller" : index==2 ? "Sony" : t.receiver?"BUZZ0002":"BUZZ0001";
                n=(uint32_t)strlen(text)*2+2;data[0]=(uint8_t)n;data[1]=3;
                for (unsigned i=0;text[i];++i) data[2+2*i]=(uint8_t)text[i];
            }
        } else if (type==1) { n=18;memcpy(data,descriptors,n); }
        else if (type==2) { n=34;memcpy(data,descriptors+18,n); }
        else {
            fprintf(stderr,"[buzzer] unsupported GET_DESCRIPTOR type=%u\n",type);
            return UINT32_MAX;
        }
        n=(std::min)(n,t.size);
        if (n && t.buffer) memcpy(vm_base+t.buffer,data,n);
    } else if (t.request[1]==9 || t.request[1]==10 || t.request[1]==11) {
        // SET_CONFIGURATION / HID SET_REPORT (LEDs) / SET_IDLE / SET_PROTOCOL.
        n=t.size;
    } else {
        fprintf(stderr,"[buzzer] unsupported control %02X/%02X\n",t.request[0],t.request[1]);
        return UINT32_MAX;
    }
    return n;
}

void worker()
{
    for (;;) {
        if (online && attach_pending.exchange(false)) {
            uint32_t ops;
            { std::lock_guard lock(state_mutex); ops=ldd; }
            for(unsigned receiver=0;receiver<receiver_count;++receiver){
                const auto probe=(int32_t)callback(vm_read32(ops+4),receiver+1);
                const auto attach=probe==0 ? (int32_t)callback(vm_read32(ops+8),receiver+1) : -1;
                {std::lock_guard lock(state_mutex);receivers[receiver].attached=probe==0&&attach==0;}
                fprintf(stderr,"[buzzer] device=%u probe=%d attach=%d receiver=%s\n",receiver+1,probe,attach,
                    probe==0&&attach==0 ? "attached" : "rejected");
            }
        }
        Transfer t{}; bool have=false;
        {
            std::lock_guard lock(state_mutex);
            if (online && !transfers.empty() && transfers.front().due<=now_ms()) {
                t=transfers.front(); transfers.pop_front(); have=true;
            }
        }
        if (have) {
            uint32_t count=0, status=0;
            if (t.interrupt) {
                if (t.size<5) { status=0x40; }
                else {
                    const auto report=buzz_usb::report_mask(buzz_input::receiver_bits(input_buttons(),t.receiver));
                    memcpy(vm_base+t.buffer,report.data(),report.size());count=5; ++reports;
                    static std::array<uint32_t,2> last={UINT32_MAX,UINT32_MAX};
                    const uint32_t report_bits=report[2]|(uint32_t(report[3])<<8)|(uint32_t(report[4])<<16);
                    if (report_bits!=last[t.receiver]) {
                        last[t.receiver]=report_bits;
                        fprintf(stderr,"[buzzer] device=%u report=%02X %02X %02X %02X %02X (#%llu)\n",t.receiver+1,
                            report[0],report[1],report[2],report[3],report[4],
                            (unsigned long long)reports.load());
                    }
                }
            } else {
                count=control_response(t);
                if (count==UINT32_MAX) {status=0x40;count=0;}
            }
            callback(t.callback,status,count,t.arg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void init(ppu_context* c) { online=true;result(c,0); }
void end(ppu_context* c) {
    online=false;attach_pending=false;
    { std::lock_guard lock(state_mutex);transfers.clear();pipes.fill(0);receivers={};ldd=0; }
    result(c,0);
}
void register_ldd(ppu_context* c) {
    const uint32_t ops=(uint32_t)c->gpr[3], vid=(uint32_t)c->gpr[4];
    const uint32_t lo=(uint32_t)c->gpr[5];
    const uint32_t hi=(uint32_t)c->gpr[6];
    fprintf(stderr,"[buzzer] RegisterExtraLdd ops=%08X vid=%04X range=%04X..%04X\n",ops,vid,lo,hi);
    if (!ops) {result(c,bad_param);return;}
    const bool match=vid==0x054c && lo<=0x1000 && (hi ? hi : lo)>=0x1000;
    if (match) {
        std::lock_guard lock(state_mutex);ldd=ops;attach_pending=true;
    }
    result(c,0); // A registered driver can have no matching device.
}
void unregister_ldd(ppu_context* c) {
    std::lock_guard lock(state_mutex);
    if ((uint32_t)c->gpr[3]==ldd) {receivers={};attach_pending=false;ldd=0;transfers.clear();pipes.fill(0);}
    result(c,0);
}
void scan(ppu_context* c) {
    const uint32_t dev=(uint32_t)c->gpr[3], previous=(uint32_t)c->gpr[4], type=(uint32_t)c->gpr[5];
    uint32_t answer=0;
    if (dev>=1&&dev<=receiver_count) {
        const auto descriptor_base=arena+(dev-1)*0x100;
        for (unsigned p=0;p<sizeof(descriptors);p+=descriptors[p]) {
            if (descriptors[p+1]==type && (!previous || descriptor_base+p>previous)) {answer=descriptor_base+p;break;}
        }
    }
    c->gpr[3]=answer;
}
void location(ppu_context* c) {
    if (c->gpr[3]<1 || c->gpr[3]>receiver_count || !c->gpr[4]) {result(c,no_device);return;}
    const uint8_t loc[7]={uint8_t(c->gpr[3]),0,0,0,0,0,0};memcpy(vm_base+(uint32_t)c->gpr[4],loc,7);result(c,0);
}
void get_private(ppu_context* c) {std::lock_guard lock(state_mutex);const auto dev=c->gpr[3];c->gpr[3]=dev>=1&&dev<=receiver_count?receivers[dev-1].private_data:0;}
void set_private(ppu_context* c) {std::lock_guard lock(state_mutex);const auto dev=c->gpr[3];if(dev<1||dev>receiver_count){result(c,no_device);return;}receivers[dev-1].private_data=(uint32_t)c->gpr[4];result(c,0);}
void open_pipe(ppu_context* c) {
    if (c->gpr[3]<1||c->gpr[3]>receiver_count) {result(c,no_device);return;}
    const unsigned receiver=unsigned(c->gpr[3]-1);
    const uint32_t desc=(uint32_t)c->gpr[4];
    std::lock_guard lock(state_mutex);
    for (unsigned i=1;i<pipes.size();++i) if (!pipes[i]) {
        pipes[i]=desc?vm_read8(desc+2):0xff;
        pipe_receivers[i]=receiver;
        fprintf(stderr,"[buzzer] OpenPipe id=%u ep=%02X\n",i,pipes[i]);result(c,i);return;
    }
    result(c,0x8011000B);
}
void close_pipe(ppu_context* c) {
    std::lock_guard lock(state_mutex);unsigned p=(unsigned)c->gpr[3];
    if(p>=pipes.size() || !pipes[p]) {result(c,no_pipe);return;}
    pipes[p]=0;
    transfers.erase(std::remove_if(transfers.begin(),transfers.end(),
                                  [p](const Transfer& t){return t.pipe==p;}),transfers.end());
    result(c,0);
}
void transfer(ppu_context* c, bool interrupt) {
    Transfer t{};t.pipe=(uint32_t)c->gpr[3];t.interrupt=interrupt;
    if (interrupt) {t.buffer=(uint32_t)c->gpr[4];t.size=(uint32_t)c->gpr[5];}
    else {
        const uint32_t req=(uint32_t)c->gpr[4];
        if (!req) {result(c,bad_param);return;}
        memcpy(t.request,vm_base+req,8);t.size=vm_read16(req+6);t.buffer=(uint32_t)c->gpr[5];
    }
    t.callback=(uint32_t)c->gpr[6];t.arg=(uint32_t)c->gpr[7];t.due=now_ms()+(interrupt?6:1);
    std::lock_guard lock(state_mutex);
    if (!online || t.pipe>=pipes.size() || !pipes[t.pipe]) {result(c,no_pipe);return;}
    t.receiver=pipe_receivers[t.pipe];
    if (t.size>4096 || (t.size && !t.buffer) || !t.callback || transfers.size()>64) {result(c,bad_param);return;}
    static unsigned diagnostic=0;
    if (diagnostic++<8) fprintf(stderr,"[buzzer] queue %s pipe=%u buf=%08X size=%u cb=%08X arg=%08X\n",
        interrupt?"interrupt":"control",t.pipe,t.buffer,t.size,t.callback,t.arg);
    transfers.push_back(t);result(c,0);
}
void interrupt_transfer(ppu_context* c) { transfer(c,true); }
void control_transfer(ppu_context* c) { transfer(c,false); }
}

extern "C" void buzz_buzzer_stats()
{
    std::lock_guard lock(state_mutex);
    fprintf(stderr,"[buzzer-progress] attached=%d receivers=%u second=%d reports=%llu pending=%zu callback=%08X\n",
        receivers[0].attached?1:0,receiver_count,receivers[1].attached?1:0,(unsigned long long)reports.load(),transfers.size(),callback_pc.load());
}

void buzz_register_virtual_buzzer()
{
    if (getenv("BUZZ_NO_VIRTUAL_BUZZER")) return;
    buzz_input_init();receiver_count=buzz_input_players()/4;
    test_input_path=getenv("BUZZ_TEST_INPUT_FILE");
    if(test_input_path && !*test_input_path)test_input_path=nullptr;
    for (uint64_t i=0;i<function_table_count;++i)
        guest_functions[(uint32_t)function_table[i].addr]=function_table[i].func;
    arena=vm_stack_allocate(&g_vm_stack_alloc,stack_size);
    if (!arena || vm_commit(arena,stack_size)!=0) {fprintf(stderr,"[buzzer] arena allocation failed\n");return;}
    for(unsigned r=0;r<receiver_count;++r)memcpy(vm_base+arena+r*0x100,descriptors,sizeof(descriptors));
    if (ppu_tls_copy_template(arena+0x2000,0x1000)!=0) {
        fprintf(stderr,"[buzzer] loaded ELF TLS does not fit the callback TLS block; adapter disabled\n");
        return;
    }
    if (const char* p=getenv("BUZZ_SCRIPT")) {
        while (*p && script_count<script.size()) {
            char* next=nullptr;
            double second=strtod(p,&next); if(next==p || *next!=':')break;
            p=next+1;uint32_t mask=(uint32_t)strtoul(p,&next,0);if(next==p)break;
            script[script_count++]={second,mask&0xfffffu};p=next;
            while(*p==',' || *p==' ')++p;
        }
        script_start=now_ms();
    }
    struct Hook {uint32_t nid;const char* name;void(*fn)(ppu_context*);};
    const Hook hooks[]={
        {0xD0E766FE,"init",init},{0x35F22AC3,"end",end},
        {0xBD554BCB,"register extra2",register_ldd},{0x7FE92C54,"register extra",register_ldd},
        {0x90460081,"unregister",unregister_ldd},{0x2FB08E1E,"scan descriptor",scan},
        {0xDB819E03,"location",location},{0x5DE3AF36,"get private",get_private},
        {0x63BFDB97,"set private",set_private},{0x254289AC,"open pipe",open_pipe},
        {0x9763E962,"close pipe",close_pipe},{0x0F411262,"interrupt",interrupt_transfer},
        {0x97CF128E,"control",control_transfer}
    };
    for (const auto& h:hooks) ps3_hle_register_ctx(h.nid,h.name,h.fn);
    fprintf(stderr,"[buzzer] virtual USB 054C:1000 enabled, %u receiver(s); mapping via input router\n",receiver_count);
    buzz_set_host_key_handler(buzz_input_key);
    std::thread(worker).detach();
}

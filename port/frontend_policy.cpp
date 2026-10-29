// BCES00647 1.02 frontend policy, NOT PSN/server emulation. All original
// game assets stay untouched. Offline identity takes the game's own no-account
// path; the store is hidden independently through its existing Lua predicate.
#include "ppu_recomp.h"
#include "frontend_policy.h"
#include "../ps3recomp/runtime/memory/vm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void ppu_register_function(uint64_t,void(*)(ppu_context*));
extern vm_stack_alloc g_vm_stack_alloc;
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
extern PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
}
namespace {
bool offline=false,hide_store=false;
uint32_t script_memory=0;
// The menu's own condition controls whether the item is added at all. Nothing
// about installed DLC, existing online services or other menu entries is changed.
constexpr char policy[]=
    "assert(Menu and Menu.Callbacks, 'Buzz port: missing menu callbacks')\n"
    "Menu.Callbacks.ShouldAllowAccessToDLC = function() return false end\n";
constexpr char chunk_name[]="@buzz-port/hide-psn-store";
bool option(const char* name){
    const auto value=getenv(name);
    if(!value)return true;
    if(!strcmp(value,"0"))return false;
    if(!strcmp(value,"1"))return true;
    fprintf(stderr,"[frontend] %s must be 0 or 1\n",name);std::exit(2);
}
void drain(ppu_context* c){while(g_trampoline_fn){auto fn=g_trampoline_fn;g_trampoline_fn=nullptr;fn(c);}}
void no_np_account(ppu_context* c){
    // Lua C ABI: one boolean result, not a fake successful PSN login.
    c->gpr[4]=0;func_0046B8F8(c);drain(c);c->gpr[3]=1;
    static bool logged=false;if(!logged){logged=true;fprintf(stderr,"[frontend] offline: Network.HasUserNpAccount=false (normal no-account boot path)\n");}
}
}
bool buzz_frontend_needs_include(){return hide_store;}
bool buzz_frontend_target_include(uint32_t L){
    if(!hide_store||!L)return false;
    const auto base=vm_read32(L+12),top=vm_read32(L+8);
    if(top<base+8||vm_read32(base+4)!=4)return false;
    const auto string=vm_read32(base);
    if(!string)return false;
    char name[128]{};
    for(unsigned i=0;i<sizeof(name)-1;++i){const auto c=char(vm_read8(string+16+i));
        if(!c)break;name[i]=(c>='A'&&c<='Z')?char(c-'A'+'a'):c;}
    const char* leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
    return !strcmp(leaf,"menu_callbacks")||!strcmp(leaf,"menu_callbacks.clu");
}
void buzz_frontend_after_include(ppu_context* original,uint32_t L){
    if(!hide_store||!script_memory)return;
    // Invoke guest Lua APIs on the SAME Lua thread after IncludeScript has
    // drained. Preserve the original PPU return context and restore Lua stack
    // by logical index (GC may relocate it), including on a compile/run error.
    auto call=*original;auto* previous=g_active_ctx;g_active_ctx=&call;
    call.gpr[3]=L;func_0046B388(&call);drain(&call);const auto top=call.gpr[3];
    call.gpr[3]=L;call.gpr[4]=script_memory;call.gpr[5]=sizeof(policy)-1;
    call.gpr[6]=script_memory+sizeof(policy);func_0047CD10(&call);drain(&call);
    int status=int(call.gpr[3]);
    if(!status){call.gpr[3]=L;call.gpr[4]=0;call.gpr[5]=0;call.gpr[6]=0;
        func_0046C3F0(&call);drain(&call);status=int(call.gpr[3]);}
    call.gpr[3]=L;call.gpr[4]=top;func_0046B3A0(&call);drain(&call);
    g_active_ctx=previous;
    fprintf(stderr,"[frontend] store predicate %s (Lua status=%d)\n",status?"NOT changed":"hidden",status);
}
void buzz_register_frontend_policy(){
    offline=option("BUZZ_OFFLINE");hide_store=option("BUZZ_HIDE_PSN_STORE");
    // Fail closed for a different title/version. These are the audited 1.02
    // Lua binding string/OPD pairs, not a scan-and-patch heuristic.
    if((offline||hide_store)&&(vm_read32(0x833098)!=0x6e5980||vm_read32(0x83f008)!=0x12b760)){
        fprintf(stderr,"[frontend] unsupported title bindings; policy disabled\n");offline=hide_store=false;return;
    }
    if(offline)ppu_register_function(0x12b760,no_np_account);
    if(hide_store){
        script_memory=vm_stack_allocate(&g_vm_stack_alloc,65536);
        if(!script_memory||vm_commit(script_memory,65536)!=0){hide_store=false;fprintf(stderr,"[frontend] script allocation failed; store unchanged\n");}
        else {memcpy(vm_base+script_memory,policy,sizeof(policy));memcpy(vm_base+script_memory+sizeof(policy),chunk_name,sizeof(chunk_name));}
    }
    fprintf(stderr,"[frontend] offline=%d hide-psn-store=%d; set each BUZZ_* option to 0 for original behavior\n",offline?1:0,hide_store?1:0);
}

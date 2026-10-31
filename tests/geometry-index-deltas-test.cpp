#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_index_deltas.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*) { abort(); }
void spu_stop(spu_context*) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-index-deltas-reference.inc"
static uint32_t random_word(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static void seed(spu_context& c, unsigned trial)
{
    memset(&c, 0, sizeof c); c.ls = c.ls_store; c.image_id = 41; c.pc = 0x7730;
    uint32_t state = trial + 1;
    for (auto& r : c.gpr) for (auto& w : r._u32) w = random_word(state);
    for (unsigned at = 0; at < SPU_LS_SIZE; at += 4) {
        auto value = random_word(state); memcpy(c.ls + at, &value, 4);
    }
    const unsigned counts[] = {0,1,2,31,32,33,63,64,65,127,128,129,255,256,257,1023,4096,65504,65535};
    c.gpr[96]._u32[0] = counts[trial % 19];
    c.gpr[88]._u32[0] = 0xe800 + (trial & 15);
    c.gpr[31]._u32[0] = 0xe800 + ((trial / 16) & 15);
    c.host_depth = trial % 3; c.policy_mode = trial % 3; c.resident_ovl = trial % 5;
    c.int_enable = trial & 1; c.mfc_tag_status = trial * 123; c.resv_valid = trial & 1;
}
static void copy(spu_context& to, const spu_context& from) { memcpy(&to, &from, sizeof to); to.ls = to.ls_store; }
static void compare(spu_context& native, spu_context& reference, unsigned trial)
{
    GeometryIndexDeltas p; const char* reason = nullptr;
    if (!read_geometry_index_deltas(native, p, &reason)) { fprintf(stderr, "unexpected reject %u: %s\n", trial, reason); abort(); }
    const auto tail = index_deltas_oracle::buzz_job_6eba_spu_func_000077F8;
    g_spu_trampoline_fn = nullptr; index_deltas_oracle::buzz_job_6eba_spu_func_00007730(&reference);
    assert(g_spu_trampoline_fn == index_deltas_oracle::buzz_job_6eba_spu_func_00007770);
    g_spu_trampoline_fn = nullptr; index_deltas_oracle::buzz_job_6eba_spu_func_00007770(&reference);
    assert(g_spu_trampoline_fn == tail);
    g_spu_trampoline_fn = nullptr; reconstruct_geometry_index_deltas(native, p, tail);
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "index deltas trial %u groups %u r%d byte%d LS%X/%u state%s\n", trial, p.groups,
                diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
        for (unsigned reg=0;reg<128;++reg) if(memcmp(&native.gpr[reg],&reference.gpr[reg],16)) {
            fprintf(stderr,"r%u native",reg);for(auto w:native.gpr[reg]._u32)fprintf(stderr," %08x",w);
            fprintf(stderr," ref");for(auto w:reference.gpr[reg]._u32)fprintf(stderr," %08x",w);fputc('\n',stderr);
        }
        abort();
    }
    assert(native.pc == 0x77f8 && g_spu_trampoline_fn == tail && native.host_depth == reference.host_depth &&
           native.image_id == reference.image_id && native.policy_mode == reference.policy_mode && native.resident_ovl == reference.resident_ovl);
}
int main()
{
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    unsigned exact = 0, aliases = 0;
    for (unsigned trial = 0; trial < 4096; ++trial) {
        seed(*native, trial); copy(*reference, *native); compare(*native, *reference, trial); ++exact;
    }
    const int shifts[] = {-80,-64,-48,-32,-16,0,16,32,48,64,80};
    for (unsigned shape = 0; shape < 11; ++shape) for (unsigned trial = 0; trial < 128; ++trial) {
        seed(*native, trial); native->gpr[31]._u32[0] = native->gpr[88]._u32[0] + shifts[shape];
        GeometryIndexDeltas p; assert(read_geometry_index_deltas(*native, p)); aliases += p.aliases;
        if(shape==6 || shape==7 || shape==8) assert(p.aliases);
        copy(*reference, *native); compare(*native, *reference, 10000 + shape * 128 + trial); ++exact;
    }
    for (unsigned trial = 0; trial < 256; ++trial) {
        seed(*native, trial); native->gpr[96]._u32[0] = trial % 33;
        native->gpr[88]._u32[0] = SPU_LS_SIZE - 64 + (trial & 15);
        native->gpr[31]._u32[0] = SPU_LS_SIZE - 16 + ((trial / 16) & 15);
        copy(*reference, *native); compare(*native, *reference, 20000 + trial); ++exact;
    }
    for (unsigned trial = 0; trial < 3; ++trial) {
        seed(*native, trial);
        FILE* fixture=fopen("analysis/native-geometry/phase3/index-audit-v1/pc_7730.bin","rb");assert(fixture);
        assert(fread(native->gpr,sizeof(native->gpr),1,fixture)==1);
        assert(fread(native->ls,SPU_LS_SIZE,1,fixture)==1 && fgetc(fixture)==EOF);fclose(fixture);
        copy(*reference,*native);compare(*native,*reference,30000+trial);++exact;
    }
    for (unsigned count : {1u, 32u, 64u}) {
        seed(*native, count); native->gpr[96]._u32[0] = count;
        native->gpr[31]._u32[0] = native->gpr[88]._u32[0] = 0xe800;
        native->gpr[10] = spu_ilh(3);
        const unsigned quads = ((count + 31) / 32 + 1) * 4;
        for(unsigned q=0;q<quads;++q)for(unsigned lane=0;lane<8;++lane){
            const unsigned v=(q+1)*17+lane,at=0xe800+q*16+lane*2;
            native->ls[at]=uint8_t(v>>8);native->ls[at+1]=uint8_t(v);
        }
        GeometryIndexDeltas p; assert(read_geometry_index_deltas(*native,p));
        reconstruct_geometry_index_deltas(*native,p,index_deltas_oracle::buzz_job_6eba_spu_func_000077F8);
        unsigned sum[8]{};
        for(unsigned q=0;q<p.groups*4;++q)for(unsigned lane=0;lane<8;++lane){
            sum[lane]=(sum[lane]+(q+1)*17+lane-3)&65535;const unsigned at=0xe800+q*16+lane*2;
            assert((unsigned(native->ls[at])<<8|native->ls[at+1])==sum[lane]);
        }
    }
    for (unsigned trial = 0; trial < 8; ++trial) {
        seed(*native, trial); switch (trial) {
        case 0:native->ls=reference->ls_store;break;
        case 1:native->gpr[96]._u32[0]=65536;break;
        case 2:native->gpr[31]._u32[0]=0xe670;break;
        case 3:native->gpr[88]._u32[0]=0xe660;break;
        case 4:native->gpr[31]._u32[0]=SPU_LS_SIZE;break;
        case 5:native->gpr[88]._u32[0]=SPU_LS_SIZE-16;break;
        case 6:native->gpr[96]._u32[0]=33;native->gpr[31]._u32[0]=SPU_LS_SIZE-16;break;
        case 7:native->gpr[88]._u32[0]=0xffffffff;break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));GeometryIndexDeltas p;p.groups=123;const char* why=nullptr;
        assert(!read_geometry_index_deltas(*native,p,&why) && why && p.groups==123);
        assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    printf("PASS index deltas: %u strict full-state/LS, %u alias cases; 3 independent numeric, 8 unchanged rejects\n", exact, aliases);
}

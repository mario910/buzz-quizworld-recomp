#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_attributes.h"
#include "../port/native_function_check.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace buzz;
static void drain(spu_context*, uint32_t);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* ctx, uint32_t stop) { drain(ctx, stop); }
void spu_img_restore(spu_context* ctx, int32_t image) { assert(ctx->image_id == image); }
void spu_stop(spu_context*) { abort(); }
void spu_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 spu_rdch(spu_context*, uint32_t) { abort(); }
void buzz_geometry_job_wrch(spu_context*, uint32_t, u128) { abort(); }
u128 buzz_geometry_job_rdch(spu_context*, uint32_t) { abort(); }
}
#include "../analysis/native-geometry/phase3/test-attributes-reference.inc"
extern "C" void spu_indirect_branch(spu_context* ctx) { auto fn = attributes_reference_lookup(ctx->pc); assert(fn); fn(ctx); }
static void drain(spu_context* ctx, uint32_t stop)
{
    unsigned steps = 0;
    while (g_spu_trampoline_fn) {
        assert(++steps < 65536); auto fn = g_spu_trampoline_fn; g_spu_trampoline_fn = nullptr; fn(ctx);
    }
    assert(ctx->pc == stop);
}
static uint32_t random_word(uint32_t& value) { value ^= value << 13; value ^= value >> 17; value ^= value << 5; return value; }
static void seed(spu_context& ctx, unsigned trial)
{
    memset(&ctx, 0, sizeof ctx); ctx.ls = ctx.ls_store; ctx.image_id = 41; ctx.pc = 0x93f0; ctx.host_depth = 1;
    uint32_t random = trial + 1;
    for (unsigned at = 0; at < SPU_LS_SIZE; ++at) ctx.ls[at] = uint8_t(random_word(random));
    for (auto& reg : ctx.gpr) for (auto& word : reg._u32) word = random_word(random);
    ctx.gpr[0]._u32[0] = 0x1380; ctx.gpr[1]._u32[0] = 0x3c000; ctx.gpr[3]._u32[0] = 0x3b000;
    ctx.gpr[4]._u32[0] = 0x10000; ctx.gpr[5]._u32[0] = 0x30000; ctx.gpr[6]._u32[0] &= ~255u;
    const unsigned counts[] = {0,1,2,3,4,5,7,8,9,12,13,16,17,31,32,33,255,256,257,512,1023,2048};
    const unsigned vertices = counts[(trial / 16) % 22];
    ctx.ls[0x3b008] = uint8_t(vertices >> 8); ctx.ls[0x3b009] = uint8_t(vertices);
    for (unsigned slot = 0; slot < 16; ++slot) {
        ctx.ls[0x3b160 + slot] = slot < trial % 16 ? uint8_t(1 + slot) : 0;
        geometry_job_put32(ctx.ls + 0x3b010 + slot * 4, 0x20000 + slot * 16);
    }
    const uint8_t selectors[3][16] = {
        {4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19},
        {8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23},
        {12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27}};
    memcpy(ctx.ls + 0xd250, selectors, sizeof selectors); geometry_job_put32(ctx.ls + 0x95c4, 0x13c);
    const uint32_t special[] = {0,0x80000000,0x7fc00001,0xff800000,0x7f800000,1,0x7fffffff,0x3f800000};
    for (unsigned i = 0; i < 8; ++i) geometry_job_put32(ctx.ls + 0x10000 + i * 4, special[(i + trial) % 8]);
    ctx.mfc_eah = trial * 0x109u; ctx.mfc_tag_status = trial * 0x127u; ctx.int_enable = trial & 1;
    ctx.resv_ea = trial * 128; ctx.resv_valid = trial & 1;
}
static void exact(spu_context& native, spu_context& reference, unsigned trial, const char* kind)
{
    const auto diff = compare_function_state(native, reference);
    if (!diff.exact()) {
        fprintf(stderr, "%s case%u differs: r%d byte%d LS=%X/%u controls=%s\n", kind, trial,
            diff.first_register, diff.first_register_byte, diff.first_ls, diff.ls_bytes, diff.first_state ? diff.first_state : "equal");
        if (diff.first_register >= 0) for (unsigned lane = 0; lane < 4; ++lane)
            fprintf(stderr, " lane%u native=%08X reference=%08X\n", lane, native.gpr[diff.first_register]._u32[lane], reference.gpr[diff.first_register]._u32[lane]);
        abort();
    }
    assert(!g_spu_trampoline_fn);
}
static void native_stream(spu_context* ctx)
{
    GeometryRawStream p; assert(read_geometry_raw_stream(*ctx,p)); decode_geometry_raw_stream(*ctx,p); SPU_RET(ctx);
}
static void raw_case(spu_context& native, spu_context& reference, unsigned trial)
{
    seed(native, trial); memcpy(&reference, &native, sizeof native); reference.ls = reference.ls_store;
    GeometryRawStream p; assert(read_geometry_raw_stream(native,p));
    buzz_job_6eba_spu_func_000093F0(&reference); drain(&reference, native.gpr[0]._u32[0]);
    decode_geometry_raw_stream(native,p); native.pc = native.gpr[0]._u32[0]; exact(native,reference,trial,"raw stream");
    if (p.vertices) for (unsigned i = 0; i < p.groups * 4; ++i) {
        assert(!memcmp(native.ls + p.destination + i * 16, native.ls + p.source + i * 12, 12));
        assert(geometry_job_be32(native.ls + p.destination + i * 16 + 12) == 0x3f800000);
    }
}
static void attributes_case(spu_context& native, spu_context& reference, unsigned trial)
{
    seed(native, trial); native.pc = 0xac68;
    native.ls[0x3b000] &= ~16u; native.ls[0x3b002] = 0; native.ls[0x3b003] = 0;
    native.gpr[6]._u32[0] = trial & 1 ? 0x18000 : 0; native.gpr[7]._u32[0] = 0x31000;
    if (trial & 2) {
        native.gpr[6]._u32[0] = 0; native.ls[0x3b000] |= 16; native.ls[0x3b004] = uint8_t((trial / 4) % 4);
        geometry_job_put32(native.ls + 0x3b088, 0);
    }
    const uint32_t dispatch[] = {0x68,0x60,0x58,0x58};
    for (unsigned i = 0; i < 4; ++i) geometry_job_put32(native.ls + 0x4104 + i * 4, dispatch[i]);
    // Two real raw streams reserve different slots and write separate arrays.
    for (unsigned slot = 0; slot < 16; ++slot) geometry_job_put32(native.ls + 0x3b010 + slot * 4, slot & 1 ? 0x28000 : 0x20000);
    memset(native.ls + 0x3b160, 0, 16);
    memcpy(&reference,&native,sizeof native); reference.ls = reference.ls_store;
    GeometryAttributes p; assert(read_geometry_attributes(native,p));
    buzz_job_6eba_spu_func_0000AC68(&reference); drain(&reference,native.gpr[0]._u32[0]);
    const buzz_geometry_attribute_functions original{buzz_job_6eba_spu_func_000093F0,buzz_job_6eba_spu_func_000040C8};
    const buzz_geometry_attribute_functions combined{native_stream,buzz_job_6eba_spu_func_000040C8};
    decode_geometry_attributes(native,p,trial & 4 ? combined : original); native.pc = native.gpr[0]._u32[0];
    exact(native,reference,trial,"attributes");
}
int main()
{
    auto native = std::make_unique<spu_context>(), reference = std::make_unique<spu_context>();
    for (unsigned trial = 0; trial < 704; ++trial) raw_case(*native,*reference,trial);
    for (unsigned trial = 0; trial < 704; ++trial) attributes_case(*native,*reference,trial);
    for (unsigned rejection = 0; rejection < 20; ++rejection) {
        seed(*native,16); GeometryRawStream p;
        switch (rejection) {
        case 0: native->ls = reference->ls_store; break;
        case 1: native->gpr[3]._u32[0] = 0xd100; break;
        case 2: native->gpr[3]._u32[0] |= 1; break;
        case 3: native->gpr[3]._u32[0] = SPU_LS_SIZE - 0x160; break;
        case 4: native->gpr[1]._u32[0] |= 1; break;
        case 5: native->gpr[1]._u32[0] = 0xe800; break;
        case 6: native->gpr[1]._u32[0] = SPU_LS_SIZE - 16; break;
        case 7: native->gpr[1]._u32[0] = 0x3b100; break;
        case 8: native->gpr[6]._u32[0] |= 1; break;
        case 9: native->ls[0x95c4] ^= 1; break;
        case 10: native->ls[0xd250] ^= 1; break;
        case 11: memset(native->ls + 0x3b160,1,16); break;
        case 12: native->gpr[4]._u32[0] = 0xd200; break;
        case 13: native->gpr[4]._u32[0] = SPU_LS_SIZE - 48; break;
        case 14: geometry_job_put32(native->ls + 0x3b010,0xd200); break;
        case 15: geometry_job_put32(native->ls + 0x3b010,SPU_LS_SIZE - 48); break;
        case 16: geometry_job_put32(native->ls + 0x3b010,0x10000); break;
        case 17: native->gpr[4]._u32[0] = 0x3b000; break;
        case 18: geometry_job_put32(native->ls + 0x3b010,0x3c000); break;
        case 19: native->gpr[4]._u32[0] = 0x3bff0; break;
        }
        memcpy(reference.get(),native.get(),sizeof(*native));
        assert(!read_geometry_raw_stream(*native,p)); assert(!memcmp(reference.get(),native.get(),sizeof(*native)));
    }
    for (unsigned trial = 0; trial < 16; ++trial) {
        seed(*native,trial); native->gpr[4]._u32[0] = 3; native->gpr[6]._u32[0] |= 255;
        GeometryRawStream p; assert(read_geometry_raw_stream(*native,p) && !p.vertices);
        memcpy(reference.get(),native.get(),sizeof(*native)); reference->ls = reference->ls_store;
        buzz_job_6eba_spu_func_000093F0(reference.get()); drain(reference.get(),native->gpr[0]._u32[0]);
        decode_geometry_raw_stream(*native,p); native->pc = native->gpr[0]._u32[0]; exact(*native,*reference,trial,"zero/unused inputs");
    }
    puts("attributes: 720 strict raw-stream + 704 strict root/original-and-native-callee cases; 20 unchanged-state rejects PASS");
}

/*
 * ps3recomp game project -- game-specific hooks and overrides.
 *
 * main.cpp is the runner and is game-agnostic. This file is where a port puts
 * everything that is not: the PRX loading hook it must answer, overrides for
 * firmware functions the HLE library gets wrong for this title, and patches
 * over recompiled code.
 *
 * It starts out as the empty version of all of that, which is what a game with
 * no lifted PRX and no overrides needs.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"
#include "ppu_recomp.h"

extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
bool buzz_edge_inflate(ppu_context*);
bool buzz_video_queue_push(ppu_context*);
void buzz_register_video_queue();
void buzz_register_virtual_buzzer();
void buzz_register_boot_trace();
void buzz_register_frontend_policy();
extern "C" void sys_timer_register_usleep_wait(uint32_t lr, int reg, uint32_t offset);
extern "C" int32_t cellPadGetData(uint32_t, void*);
extern "C" int32_t cellNetCtlGetInfo(int32_t, void*);

static void buzz_netctl_info(ppu_context* ctx)
{
    static unsigned traced = 0;
    if (getenv("BUZZ_NET_TRACE") && traced++ < 8)
        fprintf(stderr, "[buzz-net] GetInfo code=%u out=%08X lr=%08X tid=%llu\n",
            (uint32_t)ctx->gpr[3], (uint32_t)ctx->gpr[4], (uint32_t)ctx->lr,
            (unsigned long long)ctx->thread_id);
    ctx->gpr[3] = (uint64_t)(int64_t)cellNetCtlGetInfo((int32_t)ctx->gpr[3],
                                                    (void*)(uintptr_t)ctx->gpr[4]);
}

static void buzz_pad_get_data(ppu_context* ctx)
{
    const uint32_t port = (uint32_t)ctx->gpr[3];
    const uint32_t ea = (uint32_t)ctx->gpr[4];
    const int32_t rc = cellPadGetData(port, (void*)(uintptr_t)ea);
    if (rc == 0 && port == 0 && ea && getenv("BUZZ_INPUT_TRACE")) {
        static uint32_t last = 0xFFFFFFFFu;
        const uint32_t buttons = vm_read16(ea + 8) | (vm_read16(ea + 10) << 8);
        if (buttons != last) {
            last = buttons;
            fprintf(stderr, "[buzz-input] lr=%08X len=%u buttons=%04X\n",
                    (uint32_t)ctx->lr, vm_read32(ea), buttons);
            if ((uint32_t)ctx->lr == 0x29E758) {
                const uint32_t manager = (uint32_t)ctx->gpr[27];
                fprintf(stderr, "[buzz-input] manager=%08X count=%u pad0=%08X %08X %08X\n",
                    manager, vm_read32(manager), vm_read32(manager+8),
                    vm_read32(manager+12), vm_read32(manager+16));
            }
        }
    }
    ctx->gpr[3] = (uint64_t)(int64_t)rc;
}

// Fail explicitly for a missing producer, instead of reporting success and
// leaving the real SPU consumers asleep forever. Keep the ABI and queue bytes
// in the log so implementing the producer is a reproducible next milestone.
static void buzz_lfqueue_push_missing(ppu_context* ctx)
{
    if (buzz_edge_inflate(ctx)) return;
    if (buzz_video_queue_push(ctx)) return;
    static unsigned calls = 0;
    const uint32_t queue = (uint32_t)ctx->gpr[3];
    const uint32_t data = (uint32_t)ctx->gpr[4];
    if (calls++ < 6) {
        fprintf(stderr, "[buzz-blocker] _cellSpursLFQueuePushBody tid=%llu lr=%08X queue=%08X data=%08X blocking=%llu\n",
                (unsigned long long)ctx->thread_id, (uint32_t)ctx->lr,
                queue, data, (unsigned long long)ctx->gpr[5]);
        if (queue && queue < 0xE0000000u) {
            fprintf(stderr, "[buzz-blocker] queue line:");
            for (unsigned offset = 0; offset < 128; offset += 4) {
                if (!(offset & 15)) fprintf(stderr, "\n  +%02X:", offset);
                fprintf(stderr, " %08X", vm_read32(queue + offset));
            }
            fprintf(stderr, "\n");
        }
        if (data && data < 0xE0000000u) {
            fprintf(stderr, "[buzz-blocker] 32-byte work item:");
            for (unsigned offset = 0; offset < 32; offset += 4)
                fprintf(stderr, " %08X", vm_read32(data + offset));
            fprintf(stderr, "\n");
        }
    }
    ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)CELL_ENOSYS;
}

extern "C" void buzz_register_overrides(void)
{
    buzz_register_virtual_buzzer();
    buzz_register_frontend_policy();
    buzz_register_boot_trace();
    buzz_register_video_queue();
    /* The two ThreadPoolWorkers share a busy flag: one sets *(u32*)(r30+0x1A0)
     * (0x287BCC), works, clears it (0x287AD4); the other loops
     * `while (flag) usleep(2)` with LR 0x28AA8C -- ~100k spinning calls a
     * second, on the frame's critical path. Block it until the clearing store
     * instead (sys_timer.c wait sites, opt-in PS3_USLEEP_WAIT_SITES=1). Measured:
     * no FPS gain, 0.05-0.2 core saved at 4C/8T -- the worker's CPU is mostly
     * its real work; the flag is cleared by SPU job DMA, not by the PPU. */
    sys_timer_register_usleep_wait(0x0028AA8Cu, 30, 0x1A0u);
    ps3_hle_register_ctx(0x1E585B5Du, "Buzz: net control trace", buzz_netctl_info);
    ps3_hle_register_ctx(0x8A85674Du, "Buzz: Edge inflate / LFQueue diagnostic",
                        buzz_lfqueue_push_missing);
    ps3_hle_register_ctx(0x8B72CDA1u, "Buzz: pad input trace", buzz_pad_get_data);
}

/* ---------------------------------------------------------------------------
 * PRX loading
 *
 * main.cpp calls this once, after the lifted function table is registered and
 * vm_base is live, before the game runs. A title that lifts a real system PRX
 * -- libsre, which is cellSpurs and cellSync -- loads its image into guest RAM
 * here and registers its exports. Most ports never need to: the HLE library
 * answers those modules instead.
 * -----------------------------------------------------------------------*/

extern "C" void ps3_load_prx_modules(void)
{
}

/* ---------------------------------------------------------------------------
 * Example: Override a specific NID
 *
 * The module system resolves function imports by NID.  To replace a default
 * HLE stub with custom logic, register your function at init time.
 * -----------------------------------------------------------------------*/

/*
 * Example: override cellGcmSetFlipMode (NID 0xA53D12AE) to force vsync.
 *
 *   static void my_cellGcmSetFlipMode(u32 mode)
 *   {
 *       printf("[hook] cellGcmSetFlipMode: forcing VSYNC (was %u)\n", mode);
 *       // Force vsync regardless of what the game requested
 *       cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
 *   }
 *
 * Then in your init code:
 *
 *   ps3::modules::override_nid(0xA53D12AE,
 *       reinterpret_cast<void*>(my_cellGcmSetFlipMode));
 */

/* ---------------------------------------------------------------------------
 * Example: Hook into module loading
 *
 * You can intercept cellSysmoduleLoadModule to perform custom actions
 * when a particular module is loaded.
 * -----------------------------------------------------------------------*/

/*
 *   static s32 my_loadmodule_hook(u16 id)
 *   {
 *       printf("[hook] Module 0x%04X loaded\n", id);
 *
 *       if (id == CELL_SYSMODULE_AUDIO) {
 *           // Perform custom audio backend initialization
 *           init_my_audio_backend();
 *       }
 *
 *       // Call the original implementation
 *       return cellSysmoduleLoadModule(id);
 *   }
 */

/* ---------------------------------------------------------------------------
 * Game-specific patches
 *
 * Sometimes you need to patch recompiled code at specific addresses.
 * Use the patch system to modify behavior without editing generated code.
 * -----------------------------------------------------------------------*/

/*
 *   // Skip an integrity check at guest address 0x00812FA0
 *   ps3::patches::nop_range(0x00812FA0, 0x00812FB0);
 *
 *   // Replace a function call with a direct return
 *   ps3::patches::force_return(0x009A1000, 0);  // return 0
 */

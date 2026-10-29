/* Windows COFF resolves references even in discarded GCM renderer sections.
 * These are link-only traps: the Second-V CPU fixture must never render. */
#include "../ps3recomp/libs/video/rsx_commands.h"
#include "../ps3recomp/libs/video/rsx_draw_engine.h"
#include "../ps3recomp/libs/video/rsx_live_draw.h"
#include "../ps3recomp/runtime/ppu/ppu_context.h"
#include <stdlib.h>
void (*g_spu_put_hook)(u32,u32);
u32 g_rsx_last_reference;
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
void rsx_state_init(rsx_state* state) {abort();}
int rsx_process_method(rsx_state* state,u32 method,u32 data) {abort();}
void rsx_raise_user_cmd(u32 cmd) {abort();}
int rsx_draw_engine_enabled(void) {abort();}
void rsx_draw_engine_note_flip(u32 buffer) {abort();}
void rsx_draw_engine_method(u32 method,u32 arg) {abort();}
void rsx_draw_engine_set_display_buffer(u32 id,u32 loc,u32 off,u32 pitch,u32 w,u32 h) {abort();}
void rsx_draw_engine_guest_read(u32 loc,u32 off,u32 pitch,u32 w,u32 h) {abort();}
void rsx_draw_engine_guest_wrote(u32 loc,u32 off,u32 pitch,u32 w,u32 h) {abort();}
int rsx_live_draw_enabled(void) {abort();}
void rsx_live_draw_method(u32 method,u32 arg) {abort();}
void rsx_live_draw_set_display_buffer(u32 id,u32 loc,u32 off,u32 pitch,u32 w,u32 h) {abort();}
int rsx_live_draw_blit(u32 sl,u32 sa,u32 sp,u32 dl,u32 da,u32 dp,u32 w,u32 h) {abort();}
void rsx_live_draw_note_resolve(u32 sl,u32 sa,u32 dl,u32 da) {abort();}
int rsx_live_draw_resolve_blit(u32 sl,u32 sa,u32 sp,u32 ds,u32 dt,u32 dl,u32 da,u32 dp,u32 x,u32 y,u32 w,u32 h) {abort();}
void ppu_dump_guest_stack(ppu_context* ctx,const char* tag) {abort();}
void ppu_dump_bctrl_ring(uint32_t tid,const char* tag) {abort();}

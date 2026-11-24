#pragma once
#include "ppu_recomp.h"

namespace buzz {
inline void ppu_job_condition(ppu_context& c, unsigned relation)
{
    c.cr = (c.cr & ~15u) | relation | ((c.xer >> 31) & 1u);
}

void ppu_finish_scene_job(ppu_context&, uint32_t size_toc = 0x10cc, bool late_size_store = false);
struct PpuSceneTargets {
    uint32_t effect, audience, mark, clear, advance;
};
void ppu_scene_command(ppu_context&, PpuSceneTargets = {0x293278, 0x2932fc, 0x293254, 0x293288, 0x293128});
void ppu_object_dma_ranges(ppu_context&);
bool ppu_find_animation_slot(ppu_context&);
} // namespace buzz

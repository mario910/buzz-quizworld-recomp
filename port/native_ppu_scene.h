#pragma once
#include "ppu_recomp.h"

namespace buzz {
inline void ppu_job_condition(ppu_context& c, unsigned relation)
{
    c.cr = (c.cr & ~15u) | relation | ((c.xer >> 31) & 1u);
}

void ppu_finish_scene_job(ppu_context&);
void ppu_scene_command(ppu_context&);
void ppu_object_dma_ranges(ppu_context&);
bool ppu_find_animation_slot(ppu_context&);
} // namespace buzz

#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace buzz {
enum class StudioDetail { original, effects, balanced };
inline StudioDetail studio_detail()
{
    static const StudioDetail detail = [] {
        const char* mode = std::getenv("BUZZ_STUDIO_DETAIL");
        if (mode && std::strcmp(mode, "effects") == 0) return StudioDetail::effects;
        if (mode && std::strcmp(mode, "balanced") == 0) return StudioDetail::balanced;
        return StudioDetail::original;
    }();
    return detail;
}

inline bool is_studio_program(uint32_t program, uint32_t records)
{
    // The EU studio display list begins with the marker GFX_RENDERMIXSET.
    return vm_read32(program + 12) == 198 && vm_read32(program + 28) == 0x3c0 &&
           records == program + 0x3c0 && vm_read32(records) == 0x19 &&
           vm_read32(records + 4) == 0x4746585f && vm_read32(records + 8) == 0x52454e44 &&
           vm_read32(records + 12) == 0x45524d49 && vm_read32(records + 16) == 0x58534554;
}

// Take the authored disabled branches before the game submits another
// scene's worth of SPURS work for the reflection and bloom textures.
inline bool skip_studio_effect(uint32_t program, uint32_t records, uint32_t index)
{
    if ((index != 134 && index != 163) || studio_detail() == StudioDetail::original ||
        !is_studio_program(program, records)) return false;
    const uint32_t command = records + index * 16;
    const uint32_t extent = index == 134 ? 24 : 34;
    return vm_read32(command) == 0x1c && vm_read32(command + 4) == (extent << 16) &&
           vm_read32(command + 8) == (index == 134 ? 0x00add62c : 0x00add63c) &&
           vm_read32(command + 12) == 1 && vm_read32(command + extent * 16) == 0x18;
}

inline bool is_audience_light_marker(uint32_t marker, uint32_t opcode)
{
    return vm_read32(marker) == opcode && vm_read32(marker + 4) == 0x4746585f &&
           vm_read32(marker + 8) == 0x41554449 && vm_read32(marker + 12) == 0x454e4345 &&
           vm_read32(marker + 16) == 0x4c495450 && vm_read32(marker + 20) == 0x41535300;
}

inline bool skip_audience_light(uint32_t program, uint32_t records, uint32_t index)
{
    if (index != 84 || studio_detail() != StudioDetail::balanced ||
        !is_studio_program(program, records)) return false;
    // Only this draw is enclosed by GFX_AUDIENCELITPASS. The audience
    // base pass has its own draw earlier in the same program.
    const uint32_t base = records + 77 * 16;
    const uint32_t draw = records + index * 16;
    return is_audience_light_marker(draw - 32, 0x19) && is_audience_light_marker(draw + 16, 0x1a) &&
           vm_read32(base) == 0x0e && vm_read32(draw) == 0x0f &&
           vm_read32(base + 4) == vm_read32(draw + 4) &&
           vm_read32(base + 8) == 0 && vm_read32(base + 12) == 0 &&
           vm_read32(draw + 8) == 0 && vm_read32(draw + 12) == 0;
}
}

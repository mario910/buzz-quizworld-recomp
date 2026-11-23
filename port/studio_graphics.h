#pragma once
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace buzz {
enum StudioEffect : unsigned { floor_reflections = 1, bloom = 2, audience_lighting = 4 };
inline constexpr unsigned all_studio_effects = floor_reflections | bloom | audience_lighting;

inline unsigned default_studio_effects()
{
    static const unsigned defaults = []() -> unsigned {
        const char* mode = std::getenv("BUZZ_STUDIO_DETAIL");
        if (!mode || !*mode || std::strcmp(mode, "balanced") == 0) return 0;
        if (std::strcmp(mode, "effects") == 0) return audience_lighting;
        return all_studio_effects;
    }();
    return defaults;
}

// Render dispatch and the PC settings overlay can run on different threads.
inline std::atomic<unsigned>& active_studio_effects()
{
    static std::atomic<unsigned> effects{default_studio_effects()};
    return effects;
}
inline bool studio_effect_enabled(StudioEffect effect)
{
    return (active_studio_effects().load(std::memory_order_relaxed) & effect) != 0;
}
}

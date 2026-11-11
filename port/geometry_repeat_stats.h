/* Diagnostic only, BUZZ_GEOMETRY_REPEAT_STATS=1: how often an image-41
 * geometry job is handed staged inputs byte-identical to a job of one of the
 * two previous frames. It answers whether reusing a previous frame's output
 * could pay, before anyone builds that: nothing here changes what a job does.
 *
 * Three identities, each a hash over staged LS input bytes (never guest RAM):
 *   all    every input except the command template and output descriptor,
 *          whose content names this frame's output placement;
 *   rigid  `all` without the skin palette (bone matrices);
 *   mesh   `rigid` without the viewport and object matrices -- the same
 *          vertices and indices, wherever the camera or the object is.
 * Frames are told apart by the guest flip counter, so a job that straddles a
 * flip is attributed to the frame it started in. Reported every 120 frames. */
#pragma once
#include "native_geometry_job_input.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <unordered_set>

extern "C" uint32_t cellGcm_flip_request_count(void);

namespace buzz {
inline uint64_t geometry_repeat_mix(uint64_t hash, const uint8_t* bytes, size_t size)
{
    size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        uint64_t word;
        memcpy(&word, bytes + i, 8);
        hash = (hash ^ word) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 29;
    }
    for (; i < size; ++i) hash = (hash ^ bytes[i]) * 0x100000001B3ull;
    return hash;
}

inline void note_geometry_repeat(const spu_context& ctx, const GeometryJobEntry& entry)
{
    static const bool enabled = [] {
        const char* value = std::getenv("BUZZ_GEOMETRY_REPEAT_STATS");
        return value && value[0] == '1';
    }();
    if (!enabled) return;
    uint64_t all = 0x243F6A8885A308D3ull, rigid = all, mesh = all;
    uint64_t bytes = 0;
    for (unsigned role = GeometryJobEntry::indices; role < entry.input.size(); ++role) {
        const auto& input = entry.input[role];
        const uint64_t tag = (uint64_t(role) << 32) | input.size;
        const bool palette = role >= GeometryJobEntry::palette_first && role < GeometryJobEntry::skin;
        const bool matrix = role == GeometryJobEntry::viewport || role == GeometryJobEntry::object;
        const uint8_t* data = ctx.ls + input.lsa;
        all = geometry_repeat_mix(all ^ tag, data, input.size);
        if (!palette) rigid = geometry_repeat_mix(rigid ^ tag, data, input.size);
        if (!palette && !matrix) mesh = geometry_repeat_mix(mesh ^ tag, data, input.size);
        bytes += input.size;
    }
    struct State {
        std::mutex lock;
        std::unordered_set<uint64_t> seen[3][3];      /* [identity][current, previous, older] */
        uint64_t jobs = 0, hits[3] = {}, bytes = 0, skinned = 0;
        uint32_t flip = 0, frames = 0;
    };
    static State state;
    const uint32_t flip = cellGcm_flip_request_count();
    const uint64_t identity[3] = {all, rigid, mesh};
    const bool skinned = entry.skin_flavor() != 0;
    std::lock_guard<std::mutex> guard(state.lock);
    if (flip != state.flip) {
        state.flip = flip;
        for (auto& sets : state.seen) {
            sets[2].swap(sets[1]);
            sets[1].swap(sets[0]);
            sets[0].clear();
        }
        if (++state.frames % 120 == 0 && state.jobs) {
            std::fprintf(stderr,
                "[geometry-repeat] frames=120 jobs/frame=%.0f input-bytes/job=%.0f skinned=%.1f%%"
                " repeated: all=%.1f%% rigid=%.1f%% mesh=%.1f%%\n",
                state.jobs / 120.0, double(state.bytes) / state.jobs, 100.0 * state.skinned / state.jobs,
                100.0 * state.hits[0] / state.jobs, 100.0 * state.hits[1] / state.jobs,
                100.0 * state.hits[2] / state.jobs);
            state.jobs = state.bytes = state.skinned = 0;
            state.hits[0] = state.hits[1] = state.hits[2] = 0;
        }
    }
    ++state.jobs;
    state.bytes += bytes;
    state.skinned += skinned;
    for (unsigned i = 0; i < 3; ++i) {
        if (state.seen[i][1].count(identity[i]) || state.seen[i][2].count(identity[i])) ++state.hits[i];
        state.seen[i][0].insert(identity[i]);
    }
}
}

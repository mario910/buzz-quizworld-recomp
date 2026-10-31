/* Native host view of the audited image-41 SPURS input family. Reading this
 * view never writes registers/LS or accesses guest RAM. It is the input model,
 * not proof that every codec/pipeline combination has a native implementation. */
#pragma once
#include "spu_context.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace buzz {
struct GeometryJobInput {
    uint32_t lsa = 0, size = 0, ea = 0, size_flags = 0;
};

struct GeometryJobEntry {
    enum Role : unsigned {
        command = 0, output_descriptor = 1, indices = 2, palette_first = 3,
        skin = 6, auxiliary = 7, stream_a_first = 8, stream_b_first = 11,
        viewport = 14, object = 15, configuration = 16, fixed_a = 17,
        fixed_b = 18, stream_a_descriptor = 19, stream_b_descriptor = 20
    };
    std::array<GeometryJobInput, 21> input{};
    std::array<uint8_t, 16> config{};
    std::array<uint8_t, 40> user{};
    uint32_t context_lsa = 0, descriptor_lsa = 0, io_lsa = 0, io_bytes = 0;
    uint32_t output_lsa = 0, output_bytes = 0, scratch_lsa = 0, scratch_bytes = 0;
    uint32_t stack_bytes = 0, stack_top = 0, frame_lsa = 0, geometry_context_lsa = 0;
    uint32_t input_end = 0, input_ea_high = 0, dma_tag = 0, flags = 0;
    uint32_t workspace_lsa = 0, workspace_bytes = 0;
    uint32_t vertices = 0, index_count = 0;

    bool position_only() const { return (flags & 0x80) != 0; }
    bool secondary_enabled() const { return (flags & 0x100) != 0; }
    unsigned uniform_tables() const { return (config[0] & 15) + 1; }
    unsigned index_flavor() const { return config[6] >> 4; }
    unsigned skin_flavor() const { return config[6] & 15; }
};

inline uint32_t geometry_job_be32(const uint8_t* bytes)
{
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
           (uint32_t(bytes[2]) << 8) | bytes[3];
}
inline uint32_t geometry_job_be16(const uint8_t* bytes)
{
    return (uint32_t(bytes[0]) << 8) | bytes[1];
}

inline bool read_geometry_job_entry(const spu_context& ctx, GeometryJobEntry& output,
                                    const char** reason = nullptr)
{
    auto reject = [&](const char* value) { if (reason) *reason = value; return false; };
    // Specialize the reviewed job-manager ABI. Other layouts keep the lift;
    // do not mask a bad address into LS and mistake it for this input family.
    if (ctx.image_id != 41 || ctx.ls != ctx.ls_store) return reject("image/LS ownership");
    if (ctx.gpr[3]._u32[0] != 0x3fbc0 || ctx.gpr[4]._u32[0] != 0x3fc00)
        return reject("job-manager context/descriptor ABI");
    const auto* context = ctx.ls + 0x3fbc0;
    const auto* descriptor = ctx.ls + 0x3fc00;
    if ((geometry_job_be32(context + 0x14) >> 28) != 2 ||
        geometry_job_be16(context + 0x18) != 21 || geometry_job_be16(context + 0x1a) != 0 ||
        geometry_job_be16(descriptor + 0x0a) != 21 * 8 || geometry_job_be32(descriptor + 0x24) != 0 ||
        geometry_job_be16(descriptor + 8) * 16 != 0xe680)
        return reject("descriptor/input-list family");

    GeometryJobEntry entry;
    entry.context_lsa = 0x3fbc0;
    entry.descriptor_lsa = 0x3fc00;
    entry.io_lsa = geometry_job_be32(context);
    entry.io_bytes = geometry_job_be32(descriptor + 0x14);
    entry.output_lsa = geometry_job_be32(context + 0x1c);
    entry.output_bytes = geometry_job_be32(descriptor + 0x18);
    entry.scratch_lsa = geometry_job_be32(context + 0x20);
    entry.scratch_bytes = geometry_job_be16(descriptor + 0x1e) * 16;
    entry.stack_bytes = geometry_job_be16(descriptor + 0x1c) * 16;
    if (!entry.stack_bytes) entry.stack_bytes = 8192;
    entry.input_ea_high = geometry_job_be32(descriptor + 0x0c);
    entry.dma_tag = geometry_job_be32(context + 0x24);
    const auto aligned = [](uint64_t value) { return (value + 1023) & ~uint64_t(1023); };
    const uint64_t io_end = uint64_t(entry.io_lsa) + entry.io_bytes;
    const uint64_t output_base = uint64_t(entry.io_lsa) + aligned(entry.io_bytes);
    const uint64_t scratch_base = output_base + aligned(entry.output_bytes);
    const uint64_t stack_top = scratch_base + aligned(uint64_t(entry.scratch_bytes) + entry.stack_bytes);
    if (entry.io_lsa != 0xe800 || io_end > 0x3fbc0 || output_base != entry.output_lsa ||
        scratch_base != entry.scratch_lsa || stack_top > 0x3fbc0)
        return reject("declared LS region layout");
    const uint64_t sp = ctx.gpr[1]._u32[0];
    if ((sp & 15) || sp < scratch_base + entry.scratch_bytes + 0x520 || sp + 32 > stack_top)
        return reject("driver stack frame range");
    entry.stack_top = uint32_t(stack_top);
    entry.frame_lsa = uint32_t(sp - 0x520);
    entry.geometry_context_lsa = entry.frame_lsa + 0x1a0;

    uint64_t cursor = entry.io_lsa;
    for (unsigned i = 0; i < entry.input.size(); ++i) {
        const auto* list = descriptor + 0x30 + i * 8;
        auto& input = entry.input[i];
        input.size_flags = geometry_job_be32(list);
        input.size = input.size_flags & 0x7fff;
        input.ea = geometry_job_be32(list + 4);
        const uint64_t next = cursor + ((input.size + 15u) & ~15u);
        if ((input.size && !input.ea) || next > io_end)
            return reject("staged input range/address");
        // Empty entries keep a pointer too. The lift preserves EA's low
        // nibble, then advances only by the 16-byte-rounded size.
        input.lsa = uint32_t(cursor) | (input.ea & 15);
        cursor = next;
    }
    entry.input_end = uint32_t(cursor);
    // The driver frees/reuses input storage after the command-template prefix,
    // not after the last staged input. It uses entry 0's whole first word.
    const uint32_t prefix = entry.input[GeometryJobEntry::command].size_flags;
    if (prefix > entry.io_bytes) return reject("command prefix/workspace range");
    entry.workspace_lsa = entry.io_lsa + prefix;
    entry.workspace_bytes = entry.io_bytes - prefix;
    const auto& config = entry.input[GeometryJobEntry::configuration];
    if (config.size < entry.config.size() || (config.lsa & 15))
        return reject("configuration size/alignment");
    memcpy(entry.config.data(), ctx.ls + config.lsa, entry.config.size());
    // For 21 inputs and no cache list, user data starts at descriptor+D8.
    memcpy(entry.user.data(), descriptor + 0xd8, entry.user.size());
    entry.flags = geometry_job_be32(entry.user.data() + 20);
    entry.vertices = geometry_job_be16(entry.config.data() + 8);
    entry.index_count = geometry_job_be16(entry.config.data() + 10);
    output = entry; // A rejected input never publishes a partly decoded view.
    if (reason) *reason = nullptr;
    return true;
}
} // namespace buzz

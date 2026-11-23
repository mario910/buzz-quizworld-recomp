// Inner loops of the EU disc's PPU job producers. Guest VM accessors preserve
// wrapping, endian conversion, MMIO and memory-observer behavior at each access.
#pragma once
#include "native_ppu_records.h"
#include "native_ppu_scene.h"

namespace buzz {
struct PpuJobSlot {
    uint32_t table, descriptor, bytes, padded;
};

#if defined(__GNUC__)
[[gnu::always_inline]]
#endif
inline PpuJobSlot ppu_read_job_slot(uint32_t object)
{
    const uint32_t table = vm_read32(uint64_t(object) + 0x4a4);
    const unsigned selected = vm_read8(uint64_t(object) + 0x4ae);
    const uint32_t records = vm_read32(uint64_t(table) + 0x20);
    const uint32_t pointers = vm_read32(uint64_t(table) + 0x24);
    const unsigned entry = vm_read8(uint64_t(uint32_t(table + records + 24 * selected)) + 8);
    const uint32_t descriptor = vm_read32(uint32_t(table + pointers + 8 * entry));
    const uint32_t offset = vm_read32(uint64_t(descriptor) + 0x14);
    const uint32_t bytes = vm_read32(uint64_t(descriptor) + 0x18);
    const unsigned units = vm_read16(uint64_t(uint32_t(descriptor + offset)) + 4) & 0xfff;
    return {table, descriptor, bytes, (16 * units + 127u) & ~127u};
}

inline uint64_t ppu_job_pair(uint32_t size, uint32_t address)
{
    return (uint64_t(size) << 32) | address;
}

inline void ppu_pack_job_slots(ppu_context& c)
{
    const uint64_t output = c.gpr[31], list = c.gpr[5];
    const uint32_t count = uint32_t(c.gpr[6]);
    uint32_t objects[4];
    for (unsigned i = 0; i < 4; ++i)
        objects[i] = vm_read32(list + 4 * i);
    const auto first = ppu_read_job_slot(objects[0]);
    constexpr uint64_t payload = uint64_t(0xb00) << 32;
    vm_write64(output + 0x30, ppu_job_pair(first.bytes, first.descriptor));
    vm_write64(output + 0x50, payload | objects[0]);

    uint64_t scratch = first.table, descriptor_pair = 0, object_pair = 0;
    uint32_t padded[4] = {first.padded}, descriptor = first.descriptor;
    uint64_t totals[4] = {};
    // Specialize the three optional slots, each with different output registers.
#if defined(__clang__)
#pragma clang loop unroll(full)
#elif defined(__GNUC__)
#pragma GCC unroll 3
#endif
    for (unsigned i = 1; i < 4; ++i) {
        const bool active = count > i;
        descriptor_pair = first.descriptor;
        object_pair = objects[0];
        if (active) {
            const auto slot = ppu_read_job_slot(objects[i]);
            padded[i] = slot.padded;
            descriptor_pair = ppu_job_pair(slot.bytes, slot.descriptor);
            object_pair = payload | objects[i];
            scratch = slot.bytes + (i == 1 ? uint64_t(0) : uint64_t(slot.padded));
            totals[i] = uint64_t(slot.bytes) + slot.padded + (i == 1 ? 0 : 0xb00);
        }
        if (i == 3) {
            descriptor = uint32_t(descriptor_pair);
            vm_write64(output + 0x68, object_pair);
            vm_write64(output + 0x48, descriptor_pair);
        } else {
            vm_write64(output + 0x30 + 8*i, descriptor_pair);
            vm_write64(output + 0x50 + 8*i, object_pair);
        }
    }
    c.gpr[0] = padded[0] + padded[1] + padded[2] + padded[3];
    c.gpr[4] = payload;
    c.gpr[5] = objects[count > 2 ? 2 : 0];
    c.gpr[6] = uint32_t(object_pair);
    c.gpr[7] = count > 3 ? payload : 0;
    c.gpr[8] = scratch;
    c.gpr[9] = padded[1] + padded[3];
    c.gpr[10] = object_pair;
    c.gpr[11] = descriptor_pair;
    c.gpr[12] = descriptor;
    c.gpr[14] = count > 1 ? 0xb00 : 0;
    c.gpr[15] = totals[1];
    c.gpr[17] = objects[2];
    c.gpr[18] = objects[3];
    c.gpr[19] = objects[1];
    c.gpr[20] = totals[2];
    c.gpr[22] = first.bytes;
    c.gpr[23] = totals[3];
    c.gpr[24] = padded[1];
    c.gpr[25] = padded[0];
    c.gpr[26] = padded[2];
    c.gpr[27] = count > 1 ? payload : 0;
    c.gpr[29] = objects[0];
    c.gpr[30] = padded[3];
    ppu_job_condition(c, count < 3 ? 8 : count == 3 ? 2 : 4);
}

enum class PpuJobFilter { accepted, special, finished };
inline PpuJobFilter ppu_filter_jobs(ppu_context& c)
{
    auto offset = c.gpr[30], remaining = c.gpr[29];
    auto index = c.gpr[28];
    for (;;) {
        const uint32_t table = vm_read32(c.gpr[25] + 4);
        const uint32_t entry = uint32_t(offset + table);
        const uint32_t object = vm_read32(entry);
        const uint32_t descriptor = vm_read32(uint64_t(object) + 0x108);
        uint32_t value = vm_read32(uint64_t(descriptor) + 0x110);
        ++index;
        PpuJobFilter route = PpuJobFilter::finished;
        if (value >= uint32_t(c.gpr[26])) {
            value = vm_read8(uint64_t(object) + 0x116);
            if (value) route = PpuJobFilter::special;
            else {
                value = vm_read8(uint64_t(object) + 0x115);
                if (!value) route = PpuJobFilter::accepted;
            }
        }
        if (route == PpuJobFilter::finished) {
            --remaining;
            offset += 4;
            if (uint32_t(remaining) > uint32_t(c.gpr[31])) continue;
        }
        c.gpr[0] = value;
        c.gpr[5] = descriptor;
        c.gpr[6] = object;
        c.gpr[9] = entry;
        c.gpr[28] = index;
        c.gpr[29] = remaining;
        c.gpr[30] = offset;
        const bool below = route == PpuJobFilter::finished && uint32_t(remaining) < uint32_t(c.gpr[31]);
        ppu_job_condition(c, below ? 8 : route == PpuJobFilter::special ? 4 : 2);
        return route;
    }
}

// gpr[7] is the address of the stream header accumulated before dcbz.
inline void ppu_pack_stream_job(ppu_context& c)
{
    const uint64_t out = c.gpr[31], mesh = c.gpr[28], stream = c.gpr[29];
    const uint64_t header = uint32_t(c.gpr[7]);
    const uint32_t size_a = uint32_t(c.gpr[27]) << 4, size_b = uint32_t(c.gpr[24]) << 4;
    const uint32_t header_size = vm_read32(c.gpr[2] - 0x10ec);
    vm_write64(out + 0x30, ppu_job_pair(uint16_t(size_a), uint32_t(c.gpr[8])));
    vm_write64(out + 0x38, ppu_job_pair(uint16_t(size_b), uint32_t(c.gpr[30] + 0xc0)));
    const uint32_t root = vm_read32(stream + 8);
    const uint32_t command = vm_read32(c.gpr[2] - 0x10f0);
    vm_write16(out + 8, uint16_t((header_size + 15u) >> 4));
    vm_write64(out, command);
    const uint32_t vertex_bytes = 48u * vm_read16(header + 0xc);
    const uint32_t first = vm_read32(uint64_t(root) + 0x14);
    vm_write64(out + 0x40, ppu_job_pair(uint16_t(vertex_bytes), root + first));
    const int32_t type = int32_t(vm_read32(mesh + 0x14));
    uint32_t extra = 0x40;
    uint64_t pair = ppu_job_pair(extra, uint32_t(c.gpr[12]));
    if (type == 2) {
        extra = 0;
        const uint32_t current = vm_read32(stream + 8);
        pair = uint32_t(current + vm_read32(uint64_t(current) + 0x14));
    }
    vm_write64(out + 0x48, pair);
    const uint32_t arena = vm_read32(c.gpr[26]);
    vm_write64(out + 0x50, ppu_job_pair(0x30, arena + 0x2400));
    vm_write32(out + 0x14, size_a + size_b + vertex_bytes + extra);
    vm_write16(out + 0xa, 0x20);
    const uint32_t tag = vm_read32(header + 8);
    vm_write32(out + 0x24, 0x10);
    vm_write64(out + 0x58, ppu_job_pair(tag, uint32_t(header)));
    const uint32_t groups = (vm_read16(header + 0xc) + 3u) & ~3u;
    const uint32_t metadata = vm_read32(mesh + 0x88);
    const uint32_t size = groups * 112 + 0x30;
    vm_write32(out + 0x18, size);
    const uint32_t words = vm_read32(uint64_t(metadata) + 0x7c);
    vm_write16(out + 0x1e, 0x1f40);
    const uint64_t source = c.gpr[3];
    vm_write64(out + 0x68, uint32_t(source));
    vm_write64(out + 0x60, uint32_t(c.gpr[25]));
    vm_write32(out + 0x18, size + ((words * 4 + 15u) & ~15u));
    const uint64_t dest = c.gpr[4] + 0x70;
    const uint64_t row0 = vm_read64(source);
    const uint64_t row1 = vm_read64(source + 8);
    const uint64_t row2 = vm_read64(source + 16);
    const uint64_t row3 = vm_read64(source + 24);
    vm_write64(dest, row0);
    vm_write64(dest + 8, row1);
    vm_write64(dest + 16, row2);
    vm_write64(dest + 24, row3);
    const uint64_t last = vm_read64(source + 0x28);
    const uint64_t before_last = vm_read64(source + 0x20);
    vm_write64(dest + 0x28, last);
    vm_write64(dest + 0x20, before_last);
    const uint64_t flags = (c.gpr[27] << 48) | uint32_t(c.gpr[23]);
    const uint64_t counts = flags | (c.gpr[24] << 32);
    vm_write64(out + 0xa0, counts);
    c.gpr[0] = vm_read32(uint64_t(metadata) + 0x78);
    vm_write64(out + 0xa8, c.gpr[0]);
    c.gpr[5] = metadata;
    c.gpr[6] = flags;
    c.gpr[7] = row3;
    c.gpr[8] = row2;
    c.gpr[9] = dest;
    c.gpr[10] = counts;
    c.gpr[11] = before_last;
    ppu_job_condition(c, type < 2 ? 8 : type == 2 ? 2 : 4);
}

inline void ppu_collect_job_objects(ppu_context& c)
{
    uint64_t index = c.gpr[30], matches = c.gpr[29];
    do {
        const uint32_t list = vm_read32(c.gpr[23] + 0x140);
        const uint32_t object = vm_read32(uint32_t(list + uint32_t(index)*4));
        c.gpr[3] = object;
        c.gpr[4] = c.gpr[27];
        c.lr = 0x00288188;
        ppu_record_search(c);
        const uint64_t address = c.gpr[22] + int32_t(uint32_t(matches)*4);
        if (c.gpr[3]) {
            vm_write16(c.gpr[22] + int32_t(uint32_t(matches)*2) + 8, uint16_t(index));
            vm_write32(address + 0x808, object);
            ++matches;
        }
        const uint32_t offset = uint32_t(index)*16;
        const uint32_t records = vm_read32(c.gpr[23] + 0x110);
        const uint32_t selected = vm_read8(uint64_t(object) + 0x4ae);
        const uint64_t record = uint32_t(records + offset);
        vm_write32(record + 4, 0);
        vm_write8(record + 8, uint8_t(selected));
        vm_write32(record, object);
        c.gpr[0] = 0;
        c.gpr[6] = address;
        c.gpr[9] = record;
        c.gpr[10] = offset;
        c.gpr[11] = selected;
        c.gpr[28] = c.gpr[31] = object;
    } while (++index != c.gpr[26]);
    c.gpr[29] = matches;
    c.gpr[30] = index;
    ppu_job_condition(c, 2);
}

inline double ppu_track_float(uint64_t address)
{
    const uint32_t bits = vm_read32(address);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

inline void ppu_store_track_float(uint64_t address, double value)
{
    const float rounded = float(value);
    uint32_t bits;
    memcpy(&bits, &rounded, sizeof(bits));
    vm_write32(address, bits);
}

// Returns at the original expiry callback or after advancing slot eight.
inline bool ppu_advance_tracks(ppu_context& c)
{
    for (;;) {
        const uint64_t track = vm_read32(c.gpr[31]);
        c.gpr[10] = track;
        if (track) {
            const uint64_t state = vm_read32(c.gpr[31] + 0x24);
            c.gpr[8] = state;
            c.gpr[0] = vm_read8(state + 0x11a);
            if (c.gpr[0]) {
                c.gpr[9] = uint64_t(vm_read32(state + 0x224)) - 1;
                const int32_t count = int32_t(c.gpr[9]);
                vm_write32(state + 0x224, uint32_t(c.gpr[9]));
                if (count <= 0) {
                    ppu_job_condition(c, count ? 8 : 2);
                    return true;
                }
            } else {
                c.gpr[0] = vm_read8(state + 0x11b);
                c.gpr[11] = track;
                const double previous = ppu_track_float(c.gpr[30]);
                c.fpr[12] = previous;
                const double next = ppu_fp_single(ppu_fadd(c.fpr[c.gpr[0] ? 31 : 30], previous));
                c.fpr[0] = next;
                c.gpr[9] = uint32_t(track + vm_read32(track + 0x14));
                const double limit = ppu_track_float(c.gpr[9] + 4);
                c.fpr[13] = limit;
                // A quiet NaN is unordered and must not raise MXCSR invalid on comparison.
                const bool reached = limit == limit && next == next && limit <= next;
                ppu_store_track_float(c.gpr[30], next);
                if (reached) {
                    c.gpr[0] = vm_read8(state + 0x117);
                    if (c.gpr[0]) vm_write32(c.gpr[30], uint32_t(c.gpr[25]));
                    else if (previous >= limit) {
                        // The guest ORs GT into EQ before testing the result.
                        c.cr = (c.cr & ~15u) | (previous > limit ? 6u : 2u);
                        return true;
                    } else {
                        // Keep the guest float -> FPR -> float round trip when DAZ is active.
                        // Folding it to a raw copy would store a subnormal instead of signed zero.
                        volatile double fpr_limit = limit;
                        ppu_store_track_float(c.gpr[30], fpr_limit);
                    }
                }
            }
        }
        const bool last = int32_t(c.gpr[29]) == 8;
        c.gpr[29]++;
        c.gpr[31] += 4;
        c.gpr[30] += 16;
        if (last) {
            ppu_job_condition(c, 2);
            return false;
        }
    }
}
} // namespace buzz

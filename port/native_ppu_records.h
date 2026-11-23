// Pinned EU 1.00 PPU record search (0x2b6ef0) and four-slot setup (0x2b6fc0).
// Guest accesses stay ordered, including the read back of the selected byte.
#pragma once
namespace buzz
{
inline void ppu_record_search(ppu_context& c)
{
    const uint64_t object = c.gpr[3];
    const uint32_t base = uint32_t(object);
    const unsigned count = vm_read8(object + 0x4ac);
    const uint32_t enabled = uint32_t(c.gpr[4]);
    unsigned index = 0;
    uint32_t required;
    bool found;
    do {
        required = vm_read32(uint64_t(base) + 0x4b0 + 96 * index);
        found = (required & enabled) == required;
        if (found)
            break;
        ++index;
    } while (index < count);
    // The guest checks record zero even when its count byte is zero.
    const unsigned last = found ? index : index - 1;
    c.gpr[5] = object;
    c.gpr[6] = base;
    c.gpr[7] = count;
    c.gpr[8] = last + 1;
    c.gpr[9] = uint64_t(base) + 0x480 + 96 * last;
    c.gpr[10] = index;
    c.gpr[11] = required;
    c.gpr[0] = found ? uint64_t(index) : UINT64_MAX;
    c.cr = (c.cr & ~15u) | ((!found && index > count) ? 8u : 2u) | ((c.xer >> 31) & 1u);
    vm_write8(object + 0x4ae, uint8_t(c.gpr[0]));
    c.gpr[3] = vm_read8(object + 0x4ae) != 0xff;
}

// The caller admits zero to four slots. A source pointer may alias either
// output array, so each pair is published before the next input is read.
inline void ppu_prepare_slots(ppu_context& c)
{
    const unsigned count = unsigned(c.gpr[5]);
    const uint64_t base = c.gpr[3], source = c.gpr[4];
    uint32_t object = 0, descriptor = 0;
    for (unsigned i = 0; i < count; ++i) {
        object = vm_read32(uint32_t(source + 4 * i));
        vm_write32(uint64_t(uint32_t(base + 4 * i)) + 0xcb4, object);
        descriptor = vm_read32(uint64_t(object) + 0x114);
        vm_write32(uint64_t(uint32_t(base + 4 * i)) + 0xb04, descriptor);
    }
    for (unsigned i = count; i < 4; ++i) {
        vm_write32(uint32_t(base + 0xcb4 + 4 * i), 0);
        vm_write32(uint32_t(base + 0xb04 + 4 * i), 0);
    }
    if (count)
        c.gpr[7] = 4 * count;
    if (count == 4) {
        c.gpr[0] = descriptor;
        c.gpr[9] = uint32_t(source + 12);
        c.gpr[10] = uint32_t(base + 12);
        c.gpr[11] = object;
    } else {
        c.gpr[0] = 0;
        c.gpr[9] = uint32_t(base + 0xb10);
        c.gpr[10] = base + 0xcc4;
        c.gpr[11] = uint32_t(base + 0xcc0);
        c.xer |= 1u << 29;
    }
    c.ctr = 0;
    c.cr = (c.cr & ~15u) | (count < 3 ? 8u : count == 3 ? 2u : 4u) | ((c.xer >> 31) & 1u);
}
} // namespace buzz

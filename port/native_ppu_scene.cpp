// Hot sections of the EU 1.00 scene command and animation producers.
#include "native_ppu_scene.h"
#include "studio_effects.h"

namespace buzz {
void ppu_finish_scene_job(ppu_context& c)
{
    const uint64_t job = c.gpr[31], toc = c.gpr[2];
    const uint32_t parameters = uint32_t(c.gpr[24]);
    const uint32_t program_bytes = vm_read32(toc - 0x10cc);
    vm_write32(job + 0x8c, c.gpr[7]);
    vm_write32(job + 0x78, c.gpr[25] + 0x80);
    const uint32_t count = vm_read32(c.gpr[30] + 0x1a4);
    vm_write32(job + 0x7c, c.gpr[21] + 0x1a0);
    vm_write32(job + 0xa0, count);
    const uint32_t first = vm_read32(c.gpr[1] + 0x70);
    vm_write32(job + 0xac, c.gpr[8]);
    vm_write32(job + 0xa8, first);
    vm_write32(job + 0xa4, c.gpr[26]);
    vm_write32(job + 0x80, c.gpr[23]);
    vm_write32(job + 0x88, c.gpr[15]);

    const uint64_t flags = uint64_t(parameters) + 0x24c;
    vm_write8(job + 0xb4, (vm_read32(flags) >> 7) & 1);
    const uint64_t arena = vm_read32(c.gpr[27]);
    const uint32_t units = (program_bytes + 15u) >> 4;
    vm_write16(job + 8, units);
    const uint32_t offset = vm_read32(arena + 0x4ecc) << 7;
    vm_write32(job + 0x94, arena + 0x2000);
    vm_write16(job + 0xb6, c.gpr[19]);
    vm_write32(job + 0x90, arena + offset);
    // The intervening writes can alias the flags and the command pointer.
    const uint32_t enabled = vm_read32(flags);
    const uint32_t program = vm_read32(toc - 0x10d0);
    vm_write32(job + 0x9c, enabled);
    vm_write64(job, program);
    vm_write16(job + 0xa, 0x48);
    vm_write16(job + 0x1c, 0x240);
    vm_write32(job + 0x98, arena + 0x2080);
    vm_write8(job + 0xc0, 0);
    vm_write32(job + 0x10, 1);
    vm_write32(job + 0x14, 0xdf00);
    vm_write8(job + 0xb5, 0);
    vm_write32(job + 0xc, 0);
    vm_write32(job + 0x18, 0);
    vm_write16(job + 0x1e, 0x1e00);
    vm_write32(job + 0x24, 0);
    c.gpr[0] = 0x1e00;
    c.gpr[3] = c.gpr[20];
    c.gpr[4] = c.gpr[14];
    c.gpr[5] = parameters;
    c.gpr[6] = arena + 0x2080;
    c.gpr[7] = 0xdf00;
    c.gpr[8] = c.gpr[10] = 0;
    c.gpr[9] = 1;
    c.gpr[11] = units;
    c.gpr[29] = uint32_t(c.gpr[29]);
}

void ppu_scene_command(ppu_context& c)
{
    uint64_t index = c.gpr[8];
    for (;;) {
        const uint64_t record = c.gpr[29] + (uint32_t(index) << 4);
        const uint32_t opcode = vm_read32(uint32_t(record)) - 4u;
        if (opcode <= 24) {
            const uint32_t slot = opcode * 4;
            const uint64_t target = c.gpr[30] + int32_t(vm_read32(c.gpr[30] + slot));
            c.ctr = target;
            if (target == 0x00293278 && opcode == 24 &&
                skip_studio_effect(uint32_t(c.gpr[28]), uint32_t(c.gpr[29]), uint32_t(index))) {
                // Execute the original false branch, which lands on the subroutine return.
                index += vm_read16(record + 4);
                vm_write32(c.gpr[1] + 0x94, index);
                continue;
            }
            if (target == 0x002932fc && opcode == 11 &&
                skip_audience_light(uint32_t(c.gpr[28]), uint32_t(c.gpr[29]), uint32_t(index))) {
                vm_write32(c.gpr[1] + 0x94, ++index);
                continue;
            }
            // Fold control-only commands. Indirect and patched targets still
            // dispatch through the original switch using the actual table value.
            switch (uint32_t(target)) {
            case 0x00293254:
                vm_write32(c.gpr[27] + 0x160, record + 4);
                vm_write32(c.gpr[1] + 0x94, ++index);
                break;
            case 0x00293288:
                vm_write32(c.gpr[1] + 0x94, ++index);
                vm_write32(c.gpr[27] + 0x160, 0);
                break;
            case 0x00293128:
                break;
            default:
                c.gpr[0] = target;
                c.gpr[3] = uint32_t(record);
                c.gpr[8] = index;
                c.gpr[9] = slot;
                c.gpr[11] = record;
                ppu_job_condition(c, opcode == 24 ? 2 : 8);
                return;
            }
        }
        vm_write32(c.gpr[1] + 0x94, ++index);
    }
}

void ppu_object_dma_ranges(ppu_context& c)
{
    const uint64_t list = c.gpr[3], output = c.gpr[7];
    uint32_t object = 0;
    uint64_t descriptor = 0;
    for (unsigned i = 0; i < 4; ++i) {
        object = vm_read32(list + i * 4);
        if (!object) continue;
        descriptor = vm_read32(uint64_t(object) + 0x114);
        const uint64_t addresses[] = {descriptor + 0x10, descriptor + 0x20, uint64_t(object) + 0x120,
                                      descriptor + 0x30, descriptor + 0x40, uint64_t(object) + 0x130};
        for (unsigned j = 0; j < 6; ++j) {
            const uint64_t entry = uint32_t(output + i * 48 + j * 8);
            vm_write32(entry, 16);
            vm_write32(entry + 4, addresses[j]);
        }
    }
    const uint64_t last = output + 3 * 48;
    c.gpr[0] = object ? descriptor + 0x10 : last + 8;
    c.gpr[3] = list + 16;
    c.gpr[5] = uint64_t(object) + 0x120;
    c.gpr[6] = uint32_t(last);
    c.gpr[7] = last + 48;
    c.gpr[8] = uint32_t(last + 40);
    c.gpr[9] = object ? descriptor + 0x30 : 0;
    c.gpr[10] = object ? descriptor + 0x40 : last + 32;
    c.gpr[11] = object ? descriptor + 0x20 : 0;
    c.gpr[12] = uint64_t(object) + 0x130;
    c.ctr = 0;
    ppu_job_condition(c, int32_t(object) < 0 ? 8 : object ? 4 : 2);
    // The lifted epilogue restores r28..r31 directly from its entry snapshot.
}

bool ppu_find_animation_slot(ppu_context& c)
{
    uint64_t index = c.gpr[28];
    for (;;) {
        const uint32_t offset = uint32_t(index) << 2;
        const int64_t field = int32_t(offset + 0x170);
        const uint64_t row = c.gpr[27] + field;
        const uint32_t object = vm_read32(row + 4);
        if (object || uint32_t(index + 1) > 8) {
            c.gpr[0] = field;
            c.gpr[10] = object;
            c.gpr[28] = object ? index : index + 1;
            c.gpr[30] = row;
            c.gpr[31] = offset;
            ppu_job_condition(c, object && int32_t(object) < 0 ? 8 : 4);
            return object != 0;
        }
        ++index;
        if (uint32_t(index) == 2) index = 3;
    }
}
} // namespace buzz

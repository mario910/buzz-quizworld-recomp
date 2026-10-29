#pragma once
// Both audited callers immediately overwrite CR0 and volatile temporaries;
// only r3, LR and callee-saved state are live across a BUSY return.
static bool buzz_native_completion_busy(ppu_context* c, int (*probe)(uint32_t))
{
    if ((c->lr != 0x26BE10 && c->lr != 0x26BF94) ||
        probe((uint32_t)(c->gpr[3] + 0x600)) != 1) return false;
    c->gpr[3] = 0;
    return true;
}
// Quiz World 1.02 0x26A8A0: test one slot, then retire it only on success.
// Retain the guest ABI frame/TOC writes, AUTO-clear in the real SPURS handler,
// and the original retirement routine. No speculative/double consumption.
static void buzz_native_completion(ppu_context* c)
{
    const uint64_t sp = c->gpr[1], saved31 = c->gpr[31], object = c->gpr[3];
    vm_write64(sp - 0x90, sp);
    c->gpr[1] = sp - 0x90;
    c->gpr[0] = c->lr;
    vm_write64(sp - 8, saved31);
    c->gpr[31] = object;
    vm_write64(sp + 0x10, c->gpr[0]);
    c->gpr[3] = (uint32_t)(object + 0x600);
    c->gpr[4] = sp - 0x20;
    c->gpr[0] = c->gpr[5] = 1;
    vm_write16(sp - 0x20, 1);
    c->lr = 0x26A8D0;
    func_0068CF6C(c); DRAIN_TRAMPOLINE(c);
    c->gpr[2] = vm_read64(c->gpr[1] + 0x28);
    const int32_t result = (int32_t)c->gpr[3];
    c->gpr[0] = 0;
    c->cr = (c->cr & ~0xfu) | (result < 0 ? 8u : result > 0 ? 4u : 2u) |
            ((c->xer >> 31) & 1u);
    c->gpr[3] = (uint32_t)c->gpr[31];
    if (!result) {
        c->lr = 0x26A8E8;
        func_0026A7C4(c); DRAIN_TRAMPOLINE(c);
        c->gpr[0] = 1;
    }
    c->gpr[3] = (uint64_t)(int64_t)(int32_t)c->gpr[0];
    c->gpr[0] = vm_read64(c->gpr[1] + 0xa0);
    c->gpr[31] = saved31;
    c->gpr[1] += 0x90;
    c->lr = c->gpr[0];
}

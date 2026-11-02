"""Independent format/audit-reader regressions; no retail data required."""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("index_audit", ROOT / "tools/inspect/geometry-index-audit.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class GeometryIndexAuditTests(unittest.TestCase):
    def test_only_unconditional_exits_remove_lexical_tails(self):
        conditional = "        if (x) { ctx->pc = 0x77F8; g_spu_trampoline_fn = buzz_job_6eba_spu_func_000077F8; return; }"
        terminal = "        { ctx->pc = 0x7730; g_spu_trampoline_fn = buzz_job_6eba_spu_func_00007730; return; }"
        dead = "        { ctx->pc = 0xAF18; g_spu_trampoline_fn = buzz_job_6eba_spu_func_0000AF18; return; }"
        self.assertEqual(audit.reachable_text("\n".join((conditional, terminal, dead))), "\n".join((conditional, terminal)))
        self.assertEqual(audit.reachable_text("SPU_RET(ctx);\n" + dead), "SPU_RET(ctx);")

    def test_halfword_overflow_never_carries_to_neighbor(self):
        self.assertEqual(audit.halfwords((0x1234ffff,) * 4, (0x01010001,) * 4), (0x13350000,) * 4)
        self.assertEqual(audit.halfwords((0x01010001,) * 4, (0x01000000,) * 4, True), (0xffffffff,) * 4)

    @staticmethod
    def seed():
        raw = bytearray(audit.GPR + audit.LS)
        registers = {10: (3, 5, 7, 11), 12: (3, 9, 8, 7), 31: (0xe800,) * 4,
                     17: (0xe810,) * 4, 16: (0xe820,) * 4, 15: (0xe830,) * 4}
        for reg, value in registers.items():
            struct.pack_into("<4I", raw, reg * 16, *value)
        for quad in range(12):
            words = tuple((((quad + 1) * 100 + lane) << 16) | ((quad + 1) * 200 + lane) for lane in range(4))
            struct.pack_into(">4I", raw, audit.GPR + 0xe800 + quad * 16, *words)
        return raw

    def test_delta_warmup_and_eight_independent_prefix_chains(self):
        raw = self.seed()
        output = audit.delta_snapshot(raw)
        # Scalar numeric definition: every one of eight u16 lanes owns a
        # prefix chain. First warmup writes zero; the next two groups replace
        # that zero and then advance 64 bytes. A flat u16 prefix would fail.
        totals = [0] * 8
        biases = [0, 3, 0, 5, 0, 7, 0, 11]
        for quad in range(8):
            source = struct.unpack_from(">8H", raw, audit.GPR + 0xe800 + quad * 16)
            totals = [(a + value - bias) & 0xffff for a, value, bias in zip(totals, source, biases)]
            self.assertEqual(struct.unpack_from(">8H", output, audit.GPR + 0xe800 + quad * 16), tuple(totals))
        self.assertEqual(struct.unpack_from("<4I", output, 12 * 16), (0, 6, 5, 4))
        self.assertEqual(struct.unpack_from("<4I", output, 13 * 16), (128,) * 4)
        self.assertEqual(struct.unpack_from("<4I", output, 14 * 16), (192,) * 4)

    def test_all_lookahead_reads_precede_aliasing_stores(self):
        raw = self.seed()
        # All four outputs alias the first input quad. Every input must
        # already have been cached when the ordered output stores begin.
        for register in (17, 16, 15):
            struct.pack_into("<4I", raw, register * 16, *(0xe800,) * 4)
        struct.pack_into("<4I", raw, 12 * 16, *(1,) * 4)
        struct.pack_into("<4I", raw, 19 * 16, *(0x10001,) * 4)
        output = audit.delta_snapshot(raw)
        self.assertEqual(struct.unpack_from(">4I", output, audit.GPR + 0xe800), (0x10001,) * 4)
        source = struct.unpack_from(">4I", raw, audit.GPR + 0xe800)
        for reg in (49, 48, 22, 25):
            self.assertEqual(struct.unpack_from("<4I", output, reg * 16), source)

    def test_snapshot_size_count_and_source_pin_are_checked(self):
        with self.assertRaisesRegex(ValueError, "snapshot"):
            audit.delta_snapshot(bytes(8))
        raw = self.seed()
        struct.pack_into("<I", raw, 12 * 16, 2050)
        with self.assertRaisesRegex(ValueError, "iteration count"):
            audit.delta_snapshot(raw)
        with self.assertRaisesRegex(ValueError, "Unreviewed"):
            audit.source_graph(b"changed source")


if __name__ == "__main__":
    unittest.main()

import gzip
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools/inspect" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


corpus = module("test_corpus", "corpus-inspect.py")
graph = module("test_graph", "native-geometry-map.py")
formats = module("test_formats", "geometry-format-inventory.py")


class GeometryToolsTests(unittest.TestCase):
    def test_descriptor_storage_size_is_not_component_count(self):
        # A packed normal is three bytes although it expands to float4.
        data = bytes([1, 3, 1, 0, 0, 0, 0, 0, 0, 9, 4, 3, 3, 14, 0, 0])
        desc = formats.stream_descriptor(data)
        self.assertEqual(desc["attributes"][0]["byte_size"], 3)
        with self.assertRaises(ValueError):
            formats.stream_descriptor(data[:1] + b"\x02" + data[2:])

    def test_skin_pairs_are_interleaved(self):
        record = bytes([128, 3, 64, 1, 63, 5, 0, 255])
        pairs = formats.skin_pairs(record)
        self.assertEqual(pairs, ((128, 3), (64, 1), (63, 5), (0, 255)))
        self.assertEqual(sum(w for w, i in pairs), 255)

    def test_index_sections_include_both_flag_streams(self):
        data = struct.pack(">HHHBB", 3, 2, 1, 9, 0) + bytes(6)
        result = formats.index_sections(data, 9)
        self.assertEqual(result["packed_offset"], 10)  # header + 1-bit + 2-bit flags
        self.assertEqual(result["packed_end"], 14)
        with self.assertRaises(ValueError):
            formats.index_sections(data[:-1], 9)

    def test_ring_wrap_uses_guest_lane_id_and_rejects_overreservation(self):
        ls = bytearray(corpus.LS)
        struct.pack_into(">I", ls, 0x1c8, 4)
        before = struct.pack(">4I", 0x1000, 0, 0x2000, 0x1f80) + bytes(112)
        after = struct.pack(">4I", 0x1000, 0, 0x2000, 0x1100) + bytes(112)
        transfers = [(3, 0xe600, 0x3280, 128, before), (6, 0xe600, 0x3280, 128, after),
                     (2, 0xe800, 0x1000, 64, bytes(64)), (2, 0xe840, 0x1040, 128, bytes(128))]
        rows = [dict(pc=pc, lsa=f"{l:X}", ea=f"{e:X}", size=str(s))
                for pc, (_, l, e, s, _) in zip(("3948", "3AFC", "2EC8", "36D8"), transfers)]
        job = dict(entry_ls=ls, transfers=transfers)
        self.assertEqual(formats.ring_allocation(job, rows, 0x3000), "wrap")
        changed = bytearray(after)
        struct.pack_into(">I", changed, 12, 0x1180)
        transfers[1] = (6, 0xe600, 0x3280, 128, changed)
        with self.assertRaisesRegex(ValueError, "Reservation size"):
            formats.ring_allocation(job, rows, 0x3000)

    def test_rsx_nonincrementing_batches_and_truncated_parameters(self):
        block = struct.pack(">3I", 0x40081824, 0x02000000, 0x02000003)
        self.assertEqual(list(formats.rsx_methods(block)), [(0x1824, 0x02000000), (0x1824, 0x02000003)])
        with self.assertRaisesRegex(ValueError, "parameters exceed"):
            list(formats.rsx_methods(block[:-4]))

    def test_position_only_formats_and_repeated_draw_passes(self):
        desc = dict(stride=24, attributes=[dict(shader_slot=0, components=3, storage=2)])
        words = [0x41740, 0xc32]  # Driver prunes stride to 12.
        draw = [0x41808, 5, 0x40041824, 0x05000000, 0x41808, 0]
        block = struct.pack(">" + "I" * (len(words) + 2*len(draw)), *(words + 2*draw))
        job = dict(transfers=[(2, 0, 0, len(block), block)])
        self.assertEqual(formats.verify_rsx(job, [dict(pc="2968")], desc, True, 6), (1, 1))
        with self.assertRaisesRegex(ValueError, "vertex format differs"):
            formats.verify_rsx(job, [dict(pc="2968")], desc, False, 6)

    def test_empty_corpus_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as where:
            result = subprocess.run([sys.executable, str(ROOT / "tools/inspect/native-geometry-corpus.py"),
                where, "--output", str(Path(where) / "audit.json")], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No corpus jobs found", result.stderr)

    def test_user_data_after_twenty_one_dma_entries(self):
        ls = bytearray(corpus.LS)
        gpr = bytearray(corpus.GPR)
        ctx, desc = 0x3fbc0, 0x3fc00
        struct.pack_into("<I", gpr, 3 * 16, ctx)
        struct.pack_into("<I", gpr, 4 * 16, desc)
        struct.pack_into(">I", ls, ctx, 0xe800)
        struct.pack_into(">I", ls, ctx + 0x14, 2 << 28)
        struct.pack_into(">H", ls, ctx + 0x18, 21)
        struct.pack_into(">H", ls, desc + 10, 21 * 8)
        struct.pack_into(">Q", ls, desc + 0x30, (16 << 32) | 0x12340000)
        ls[desc + 0xd8:desc + 0xdc] = b"TEST"
        raw = struct.pack("<IIiIII", 0x434a5a42, 1, 41, 0x1234, 0, corpus.LS)
        raw += gpr + ls + struct.pack("<I", 0) + gpr + ls + bytes(28)
        with tempfile.TemporaryDirectory() as where:
            path = Path(where) / "synthetic.bjc.gz"
            path.write_bytes(gzip.compress(raw))
            job = corpus.load(path)
            layout = corpus.layout(job)
            self.assertEqual(layout["size_desc"], 256)
            self.assertEqual(layout["user_offset"], 0xd8)
            self.assertEqual(layout["user"][:4], b"TEST")
            self.assertEqual(layout["dma"][0], (16, 0x12340000))
            path.write_bytes(gzip.compress(raw + b"unexpected"))
            with self.assertRaises(ValueError):
                corpus.load(path)

    def test_call_edges_are_distinct_from_trampoline_edges(self):
        source = """void buzz_job_6eba_spu_func_00000010(spu_context* ctx) {
    buzz_job_6eba_spu_func_00000020(ctx);
    ctx->pc = 0x30; g_spu_trampoline_fn = buzz_job_6eba_spu_func_00000030; return;
}
void buzz_job_6eba_spu_func_00000020(spu_context* ctx) {
    ctx->gpr[3] = spu_ls_read128(ctx, ctx->gpr[4]._u32[0]);
    SPU_RET(ctx);
}
void buzz_job_6eba_spu_func_00000030(spu_context* ctx) {
    g_spu_trampoline_fn = spu_indirect_branch; return;
}
"""
        with tempfile.TemporaryDirectory() as where:
            path = Path(where) / "spu_recomp.c"
            path.write_text(source)
            result = graph.extract(path)
        first = result["blocks"]["00000010"]
        self.assertEqual(first["calls"], ["00000020"])
        self.assertEqual(first["branches"], ["00000030"])
        self.assertTrue(result["blocks"]["00000030"]["indirect"])
        second = result["blocks"]["00000020"]
        self.assertEqual(second["registers_read"], [4])
        self.assertEqual(second["registers_written"], [3])
        self.assertEqual(result["functions"]["00000020"]["blocks"], ["00000020"])


if __name__ == "__main__":
    unittest.main()

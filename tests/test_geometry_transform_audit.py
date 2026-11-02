import importlib.util
from pathlib import Path
import struct
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('transform_audit',ROOT/'tools/inspect/geometry-transform-audit.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)


class TransformAuditTests(unittest.TestCase):
    def test_graph_and_source_pin(self):
        raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
        self.assertEqual(graph['control_blocks'],18)
        self.assertEqual(graph['retained_vertex_blocks'],['6188','63E0','6490','65EC','65FC'])
        self.assertEqual(graph['edges']['6744'],['66B8','6790','CC70'])
        with self.assertRaises(ValueError):audit.source_graph(raw+b'\n')

    def test_unconditional_tail_excludes_next_function(self):
        body='{ ctx->pc = 0x63E0; g_spu_trampoline_fn = buzz_job_6eba_spu_func_000063E0; return; }\n{ ctx->pc = 0x6608; g_spu_trampoline_fn = buzz_job_6eba_spu_func_00006608; return; }'
        self.assertNotIn('6608',audit.reachable(body))

    def test_all_halfword_triangle_counts(self):
        for n in range(65536):self.assertEqual(audit.div3(n),n//3,n)

    def test_byte_selectors_preserve_padding(self):
        first=bytes(range(16));old=bytes(range(64,80))
        for size in (2,4):
            for bias in range(16):
                expected=bytearray(old);at=bias&~(size-1);expected[at:at+size]=first[4-size:4]
                actual=audit.shuffle(audit.words(first),audit.words(old),audit.selector(bias,size))
                self.assertEqual(audit.data(actual),expected)

    def test_frame_preserves_link_and_pic_upper_words(self):
        regs=[audit.splat(i*17) for i in range(128)];regs[1]=(0x20003,1,2,3);regs[4]=audit.splat(3)
        before=audit.packed(regs,bytearray(audit.LS));r,ls=audit.unpack(audit.frame_snapshot(before))
        self.assertEqual(r[1],audit.add(regs[1],audit.splat(-480)))
        self.assertEqual(r[126],(0,(-0x6618)&audit.MASK,(-0x6618)&audit.MASK,(-0x6618)&audit.MASK))
        self.assertEqual(struct.unpack_from('>4I',ls,(regs[1][0]+16)&0x3fff0),regs[0])
        self.assertEqual(struct.unpack_from('>4I',ls,(regs[1][0]-0x190)&0x3fff0),regs[126])

    def test_publication_is_observed_by_late_return_load(self):
        regs=[audit.splat(i*17) for i in range(128)];regs[81]=audit.splat(0x30000)
        regs[1]=audit.splat(0x30170-480);regs[50]=audit.splat(7);ls=bytearray(audit.LS)
        struct.pack_into('>4I',ls,0x30180,0x12345678,0xabcdefff,17,19)
        after=audit.finish_snapshot(audit.packed(regs,ls));r,changed=audit.unpack(after)
        self.assertEqual(struct.unpack_from('>H',changed,0x30180)[0],21)
        final,_=audit.unpack(audit.finish_snapshot(after,True))
        self.assertEqual(final[0],struct.unpack_from('>4I',changed,0x30180))
        self.assertEqual(final[3],audit.splat(21))


if __name__=='__main__':unittest.main()

import importlib.util
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[1]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
audit=module('conversion_audit','tools/inspect/geometry-conversion-audit.py')
adapter=module('conversion_adapter','tools/codegen/generate-geometry-phase3.py')


class ConversionAuditTests(unittest.TestCase):
    def test_pinned_full_standard_inventory_and_dead_getter_tail(self):
        raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
        self.assertEqual(graph['standard_nonreport_blocks'],73)
        self.assertEqual(sum(map(len,graph['field_regions'].values())),37)
        self.assertEqual(graph['edges']['2728'],[])
        self.assertEqual(graph['dead_lexical_targets']['2728'],['2738'])
        self.assertEqual(graph['edges']['54E8'],['5500'])
        self.assertIn('4880',graph['edges']['43D0'])
        self.assertIn('56C8',graph['edges']['5558'])
        self.assertNotIn('6158',graph['edges'])
        with self.assertRaisesRegex(ValueError,'Unreviewed'):audit.source_graph(raw+b'\n')

    def test_three_tables_and_every_corrupt_slot(self):
        r=[audit.scalar.splat(i) for i in range(128)];ls=bytearray(audit.scalar.LS)
        for base,values in audit.TABLES.items():struct.pack_into('>'+str(len(values))+'I',ls,base,*values)
        raw=audit.scalar.packed(r,ls);tables=audit.validate_tables(raw)
        self.assertEqual(tables['5604'],['56C8','5E28','5760','5A30'])
        self.assertEqual(tables['4104'],['416C','4164','415C','415C'])
        for base,values in audit.TABLES.items():
            for i in range(len(values)):
                bad=bytearray(raw);bad[2048+base+i*4+3]^=1
                with self.assertRaisesRegex(ValueError,'table changed'):audit.validate_tables(bad)

    def test_prefix_full_vectors_and_frame_bytes(self):
        r=[(i,i+100,i+200,i+300) for i in range(128)];r[1]=(0x20003,1,2,3);ls=bytearray(audit.scalar.LS)
        before=list(r);after,changed=audit.scalar.unpack(audit.frame(audit.scalar.packed(r,ls),0x54e8))
        self.assertEqual(after[81],before[3]);self.assertEqual(after[126],(0x5500,0,0,0))
        self.assertEqual(after[6],(0x5500,)*4)
        self.assertEqual(struct.unpack_from('>4I',changed,0x1ffe0),before[81])
        self.assertEqual(struct.unpack_from('>4I',changed,0x1ffc0),before[126])

    def test_open_reads_flags_after_overlapping_saved_link_store(self):
        r=[audit.scalar.splat(i) for i in range(128)];r[1]=(0x20000,10,20,30);r[3]=(0x20010,1,2,3)
        r[0]=(0x10000000,11,22,33);r[126]=(0x5500,0,0,0);r[6]=(0x5500,)*4
        ls=bytearray(audit.scalar.LS);struct.pack_into('>4I',ls,0x20060,101,102,103,104)
        after,changed=audit.scalar.unpack(audit.frame(audit.scalar.packed(r,ls),0x5500))
        self.assertEqual(after[5],r[0]);self.assertEqual(after[2][0],16)
        self.assertEqual(after[82],(102,103,104,101))
        self.assertEqual(after[126],(0,0xffffab00,0xffffab00,0xffffab00))
        self.assertEqual(after[1],audit.scalar.add(r[1],audit.scalar.splat(-96)))

    def test_return_loads_late_link_and_preserves_full_output_vector(self):
        r=[audit.scalar.splat(i) for i in range(128)];r[1]=(0xffffffA0,1,2,3);r[82]=(0x1010,0x2020,0x3030,0x4040)
        ls=bytearray(audit.scalar.LS);link=(0x5538,0xabcdef01,0x87654321,17);struct.pack_into('>4I',ls,16,*link)
        after,_=audit.scalar.unpack(audit.frame(audit.scalar.packed(r,ls),0x5538))
        self.assertEqual(after[0],link);self.assertEqual(after[3],r[82]);self.assertEqual(after[1],(0,97,98,99))

    def test_callback_guards_preserve_all_original_guest_operations(self):
        source=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_text();guarded,counts=adapter.guard_custom_callbacks(source)
        self.assertEqual(counts,{'indirect_call':1,'callback_boundaries':2})
        self.assertNotIn('spu_indirect_branch(ctx)',guarded)
        self.assertIn('buzz_geometry_job_indirect_call(ctx); spu_drain_call(ctx, 0x5648)',guarded)
        self.assertIn('buzz_geometry_job_callback_boundary(ctx);\n        SPU_RET_REG(ctx, 12);',guarded)
        self.assertIn('buzz_geometry_job_callback_boundary(ctx);\n        ctx->pc = ctx->gpr[5]._u32[0];',guarded)
        restored=guarded.replace('        buzz_geometry_job_callback_boundary(ctx);\n','').replace('buzz_geometry_job_indirect_call(ctx)','spu_indirect_branch(ctx)')
        self.assertEqual(restored,source)

    def test_missing_callback_marker_is_rejected(self):
        source=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_text().replace('SPU_RET_REG(ctx, 12);','SPU_RET(ctx);')
        with self.assertRaises(SystemExit):adapter.guard_custom_callbacks(source)


if __name__=='__main__':unittest.main()

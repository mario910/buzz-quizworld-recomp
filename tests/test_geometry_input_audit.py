import importlib.util,struct,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('input_audit',ROOT/'tools/inspect/geometry-input-audit.py');audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
class InputAuditTests(unittest.TestCase):
 def test_complete_pinned_standard_graph(self):
  raw=(ROOT/'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes();graph=audit.source_graph(raw)
  self.assertEqual((graph['standard_blocks'],graph['data_blocks'],graph['frame_bytes']),(23,17,448));self.assertEqual(graph['frontiers'],['95DC'])
  self.assertEqual([f['normal_block_count'] for f in graph['formats']],[2,3,5,3,2,2])
  self.assertEqual(graph['edges']['2760'],[]);self.assertEqual(graph['edges']['9498'],[])
  self.assertEqual(graph['edges']['9580'],['95DC','9700','97F8','9E70','A390','A6A8','A8F8'])
  with self.assertRaisesRegex(ValueError,'Unreviewed'):audit.source_graph(raw+b'\n')
 def test_all_six_dispatch_slots_and_size(self):
  data=bytearray(2048+0x40000)
  for i,target in enumerate(audit.ROOTS):struct.pack_into('>I',data,2048+0x95c4+i*4,target-0x95c4)
  self.assertEqual(audit.validate_table(data),['9700','A8F8','A6A8','A390','9E70','97F8'])
  for slot in range(6):
   bad=bytearray(data);bad[2048+0x95c4+slot*4+3]^=1
   with self.assertRaisesRegex(ValueError,'table changed'):audit.validate_table(bad)
  with self.assertRaisesRegex(ValueError,'size'):audit.validate_table(data[:-1])
if __name__=='__main__':unittest.main()

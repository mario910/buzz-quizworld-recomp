import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ring_audit', ROOT / 'tools/inspect/geometry-ring-audit.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class RingAuditTests(unittest.TestCase):
    def test_pinned_graph_includes_computed_disable_target(self):
        raw = (ROOT / 'port/spu/buzz_job_6eba/spu_recomp.c').read_bytes()
        graph = audit.source_graph(raw)
        self.assertEqual(graph['blocks'], 39)
        self.assertEqual(graph['edges']['3940'], ['3948'])
        self.assertEqual(graph['edges']['3CEC'], [])
        self.assertEqual(graph['dead_lexical_targets']['3CEC'], ['3B28'])
        self.assertEqual(graph['computed_targets']['3CEC'], '3CF4 (not registered)')
        for pc in ('3DD0', '4078'):
            self.assertIn('298', graph['edges'][pc])
            self.assertIn('CC70', graph['edges'][pc])
        self.assertIn('rdch:SPU_RdDec', graph['channel_sites']['40B4'])
        self.assertEqual(graph['intra_block_loops'], {'3BDC': ['3BDC'], '40B4': ['40B4']})
        with self.assertRaisesRegex(ValueError, 'Unreviewed'):
            audit.source_graph(raw + b'\n')

    def test_indirect_exit_cuts_dead_tail_but_keeps_conditional_branches(self):
        conditional = 'if (ctx->gpr[4]._u32[0] == 0) { ctx->pc = 0x3930; g_spu_trampoline_fn = target; return; }'
        indirect = 'ctx->int_enable = 1; ctx->pc = ctx->gpr[2]._u32[0]; g_spu_trampoline_fn = spu_indirect_branch; return;'
        body = conditional + '\n' + indirect + '\nDEAD_NEXT_FUNCTION'
        live = audit.reachable_text(body)
        self.assertIn(conditional, live)
        self.assertIn(indirect, live)
        self.assertNotIn('DEAD_NEXT_FUNCTION', live)

    def fixture(self, directory, mutate=None):
        def write(name, fields, rows):
            with (directory / name).open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(fields)
                writer.writerows(rows)
        entries = {0x10: 19508, 0x37c0: 7201, 0x38d0: 7201, 0x3e04: 20}
        write('all.entries.csv', ['pc', 'entries'], [(f'{pc:X}', n) for pc, n in entries.items()])
        write('all.edges.csv', ['source', 'target', 'count'],
              [('3800', '3930', 7201), ('3940', '3948', 7201), ('3B18', '3B28', 7201)])
        rows = []
        for i in range(7201):
            job = f'synthetic_{i}'
            rows.append([job, '3948', 'D0', 'E600', '100000', '128', '0'])
            if i < 20:
                rows.append([job, '3E04', '40', '10380', '200000', '4', '20'])
            rows.append([job, '3AFC', 'B0', 'E600', '100000', '128', '0'])
        if mutate:
            mutate(rows)
        for name in ('all.commands.csv', 'all.dma.csv'):
            write(name, ['job', 'pc', 'cmd', 'lsa', 'ea', 'size', 'tag'], rows)
        (directory / 'replay.log').write_text('[replay] 19508 exact, 0 with concurrent input changes, 0 FAILED (58195 transfers)\n')

    def test_original_trace_reports_no_native_acceptance(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.fixture(path)
            result = audit.original_trace(path)
        self.assertEqual(result['paired_lane_reservations'], 7201)
        self.assertEqual(result['allocator_transfers'], 14422)
        self.assertFalse(result['native_acceptance'])
        self.assertIn('3CEC', result['cold_blocks'])

    def test_same_shape_wrong_publication_ea_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.fixture(path, lambda rows: rows[2].__setitem__(4, '100080'))
            with self.assertRaisesRegex(ValueError, 'publication does not match'):
                audit.original_trace(path)

    def test_same_shape_refresh_from_another_job_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.fixture(path, lambda rows: rows[1].__setitem__(0, 'different_job'))
            with self.assertRaisesRegex(ValueError, 'refresh outside'):
                audit.original_trace(path)

    def test_same_totals_overlapping_reservations_are_rejected(self):
        def reorder(rows):
            rows[2], rows[3] = rows[3], rows[2]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.fixture(path, reorder)
            with self.assertRaisesRegex(ValueError, 'Overlapping'):
                audit.original_trace(path)

    def test_incomplete_whole_job_replay_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            self.fixture(path)
            (path / 'replay.log').write_text('[replay] FAILED\n')
            with self.assertRaisesRegex(ValueError, 'whole-job replay did not pass'):
                audit.original_trace(path)


if __name__ == '__main__':
    unittest.main()

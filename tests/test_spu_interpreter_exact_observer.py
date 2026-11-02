"""Reject incomplete evidence and exercise suspension cleanup on every exit."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[1] / 'perf/observe-spu-interpreter-exact.py'
spec = importlib.util.spec_from_file_location('exact_observer', path)
observer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(observer)


def values(rows=()):
    keys, counts = [0] * 4096, [0] * 4096
    for slot, (image, page, count) in enumerate(rows):
        keys[slot] = (((image & 0xffffffff) << 8) | (page >> 10)) + 1
        counts[slot] = count
    return dict(descriptor=struct.pack('<Q', observer.DESCRIPTOR), enabled=struct.pack('<i', 1),
                flags=bytes(4), unbucketed=bytes(8), keys=struct.pack('<4096Q', *keys),
                steps=struct.pack('<4096Q', *counts))


def binding():
    offset, symbols = 0x1000, {}
    for name, size in observer.LAYOUT.items():
        symbols[name] = dict(rva=offset, size=size)
        offset += size
    return dict(image_size=0x20000, interpreter_symbols=symbols,
                symbols=dict(g_render_perf_epoch=0x12000, g_render_perf_serial=0x12008))


class FakeThreads:
    def __init__(self, inventories=({1, 2},), fail_suspend=None, exited=False, fail_resume=None):
        self.inventories = iter(inventories)
        self.last = None
        self.fail_suspend, self.fail_resume = fail_suspend, fail_resume
        self.exited = exited
        self.held, self.resumed, self.census = {}, [], 0

    def enumerate(self):
        self.census += 1
        self.last = next(self.inventories, self.last)
        return self.last

    def suspend(self, tid):
        if tid == self.fail_suspend:
            raise OSError('injected suspend failure')
        self.held[tid] = 1  # a pre-existing suspension, which must remain
        return tid

    def active(self, item):
        return not self.exited

    def resume(self, item):
        self.resumed.append(item)
        del self.held[item]
        if item == self.fail_resume:
            raise OSError('injected resume failure')


class ExactObserverTests(unittest.TestCase):
    def test_explicit_enabled_zero(self):
        self.assertEqual(observer.interpret(values()), dict(total=0, buckets=[], all_images_zero_proven=True))

    def test_one_instruction_and_image_limits(self):
        rows = [(41, 0x9000, 1), (-1, 0x3fc00, 3), (-2147483648, 0, 7), (2147483647, 0x400, 9)]
        result = observer.interpret(values(rows))
        self.assertEqual(result['total'], 20)
        self.assertFalse(result['all_images_zero_proven'])
        self.assertEqual(result['buckets'], [dict(image=i, page=p, steps=n) for i, p, n in sorted(rows)])

    def test_not_armed_and_disabled_cannot_prove_zero(self):
        for mode in (-1, 0, 2):
            sample = values()
            sample['enabled'] = struct.pack('<i', mode)
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, 'disabled or unarmed'):
                observer.interpret(sample)

    def test_wrong_descriptor_or_incomplete_bytes(self):
        for key in observer.LAYOUT:
            sample = values()
            sample[key] = sample[key][:-1]
            with self.subTest(key=key), self.assertRaises(ValueError):
                observer.interpret(sample)
        sample = values()
        sample['descriptor'] = bytes(8)
        with self.assertRaisesRegex(ValueError, 'ABI'):
            observer.interpret(sample)

    def test_every_loss_flag_and_unbucketed_count_is_rejected(self):
        for flags in (1, 2, 4, 7, 8):
            sample = values()
            sample['flags'] = struct.pack('<I', flags)
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                observer.interpret(sample)
        sample = values()
        sample['unbucketed'] = struct.pack('<Q', 1)
        with self.assertRaises(ValueError):
            observer.interpret(sample)

    def test_uncommitted_claim_does_not_count(self):
        self.assertEqual(observer.interpret(values([(41, 0x9000, 0)]))['total'], 0)

    def test_invalid_and_duplicate_bucket_keys(self):
        for keys in ((0, 1), ((1 << 40) + 1, 1)):
            sample = values()
            sample['keys'] = struct.pack('<4096Q', keys[0], *([0] * 4095))
            sample['steps'] = struct.pack('<4096Q', keys[1], *([0] * 4095))
            with self.assertRaises(ValueError):
                observer.interpret(sample)
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            observer.interpret(values([(41, 0x9000, 0), (41, 0x9000, 1)]))

    def test_total_overflow_is_rejected_without_read_api_flag(self):
        with self.assertRaisesRegex(ValueError, 'overflow'):
            observer.interpret(values([(41, 0, (1 << 64) - 1), (41, 0x400, 1)]))

    def test_binding_rejects_missing_misaligned_overlapping_outside_spans(self):
        observer.validate_binding(binding())
        for kind in ('missing', 'size', 'alignment', 'overlap', 'outside', 'renderer_overlap', 'renderer_alias'):
            sample = binding()
            s = sample['interpreter_symbols']
            if kind == 'missing': del s['keys']
            if kind == 'size': s['keys']['size'] -= 8
            if kind == 'alignment': s['keys']['rva'] += 1
            if kind == 'overlap': s['keys']['rva'] = s['steps']['rva']
            if kind == 'outside': s['keys']['rva'] = sample['image_size']
            if kind == 'renderer_overlap': sample['symbols']['g_render_perf_epoch'] = s['keys']['rva']
            if kind == 'renderer_alias': sample['symbols']['g_render_perf_epoch'] = sample['symbols']['g_render_perf_serial']
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                observer.validate_binding(sample)

    def test_new_threads_require_two_unchanged_censuses(self):
        api = FakeThreads(({1}, {1, 2}, {1, 2}, {1, 2}))
        with observer.frozen_threads(api) as count:
            self.assertEqual(count, 2)
            self.assertEqual(api.census, 4)
        self.assertEqual(api.resumed, [2, 1])
        self.assertFalse(api.held)

    def test_partial_suspend_failure_restores_previous_threads(self):
        api = FakeThreads(fail_suspend=2)
        with self.assertRaises(OSError), observer.frozen_threads(api):
            self.fail('Must not read before all threads are frozen')
        self.assertEqual(api.resumed, [1])
        self.assertFalse(api.held)

    def test_read_failure_restores_all_threads(self):
        api = FakeThreads()
        with self.assertRaisesRegex(ValueError, 'partial read'), observer.frozen_threads(api):
            raise ValueError('partial read')
        self.assertEqual(api.resumed, [2, 1])

    def test_exited_or_disappearing_thread_invalidates_snapshot(self):
        for api in (FakeThreads(exited=True), FakeThreads(({1, 2}, {1}))):
            with self.assertRaises(ValueError), observer.frozen_threads(api):
                self.fail('Thread changed identity')
            self.assertFalse(api.held)

    def test_resume_failure_does_not_skip_remaining_cleanup(self):
        api = FakeThreads(fail_resume=2)
        with self.assertRaisesRegex(RuntimeError, 'restore all'), observer.frozen_threads(api):
            pass
        self.assertEqual(api.resumed, [2, 1])

    def test_unstable_inventory_times_out_and_cleans_up(self):
        api = FakeThreads()
        with patch.object(observer.time, 'monotonic', side_effect=(0, 0, 3)):
            with self.assertRaisesRegex(ValueError, 'stabilize'), observer.frozen_threads(api):
                self.fail('Deadline expired')
        self.assertFalse(api.held)


if __name__ == '__main__':
    unittest.main()

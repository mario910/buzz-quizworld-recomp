import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('profiles', Path(__file__).resolve().parents[1]/'perf/compare-renderer-profiles.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
LOG = '''[pc-settings] configured=1 volume=0% cap=0 fullscreen=0 render_scale=100%
[RSX vulkan] using device 0: NVIDIA GeForce RTX 4060 Ti
[RSX vulkan] experimental small-draw batch size 512
[RSX vulkan] two-context asynchronous draw batches enabled
[RSX vulkan] window swapchain 1280x720, 3 images, format 44
[RSX vulkan] window present mode mailbox
[job-pool] idle lanes poll 1000 us before sleeping (automatic, 24 logical processors)
'''


class ProfileTests(unittest.TestCase):
    def test_device_index_and_policy_origin_are_not_settings(self):
        other = LOG.replace('device 0:', 'device 1:').replace('automatic,', 'SPURS_POOL_SPIN_US,')
        self.assertEqual(module.differences([module.profile(LOG), module.profile(other)]), {})

    def test_actual_extent_not_requested_size(self):
        other = LOG.replace('swapchain 1280x720', 'swapchain 1024x576')
        profiles = [module.profile(LOG), module.profile(other)]
        self.assertEqual(module.differences(profiles), {'swapchain': ['1024x576', '1280x720']})

    def test_policy_experiment_requires_explicit_exception(self):
        profiles = [module.profile(LOG), module.profile(LOG.replace('poll 1000 us', 'poll 0 us'))]
        self.assertIn('pool_spin_us', module.differences(profiles))
        self.assertEqual(module.differences(profiles, ['pool_spin_us']), {})

    def test_missing_or_changed_extent_is_rejected(self):
        for log in [LOG.replace('window swapchain', 'requested size'),
                    LOG+'[RSX vulkan] window swapchain 1024x576, 3 images\n']:
            with self.assertRaises(ValueError):
                module.profile(log)

    def test_missing_context_marker_is_rejected(self):
        with self.assertRaises(ValueError):
            module.profile(LOG.replace('two-context asynchronous draw batches enabled', ''))


if __name__ == '__main__':
    unittest.main()

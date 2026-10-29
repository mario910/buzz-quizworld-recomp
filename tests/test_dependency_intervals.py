import importlib.util
from pathlib import Path
import random
import unittest

spec = importlib.util.spec_from_file_location("dependencies", Path(__file__).resolve().parents[1] / "inspect-geometry-dependencies.py")
dependencies = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dependencies)


class IntervalTests(unittest.TestCase):
    def test_boundaries(self):
        self.assertEqual(dependencies.subtract([(0, 10)], [(2, 3), (3, 5), (8, 12)]), [(0, 2), (5, 8)])
        self.assertEqual(dependencies.subtract([(0, 10)], [(-1, 11)]), [])
        self.assertEqual(dependencies.subtract([(0, 10)], [(10, 11)]), [(0, 10)])
        self.assertEqual(dependencies.subtract([], [(0, 1)]), [])

    def test_random_against_discrete_oracle(self):
        rng = random.Random(71391)
        for _ in range(10000):
            a = [tuple(sorted(rng.sample(range(41), 2))) for _ in range(rng.randrange(10))]
            b = [tuple(sorted(rng.sample(range(41), 2))) for _ in range(rng.randrange(10))]
            occupied_a = {n for lo, hi in a for n in range(lo, hi)}
            occupied_b = {n for lo, hi in b for n in range(lo, hi)}
            remaining = dependencies.subtract(a, b)
            actual = {n for lo, hi in remaining for n in range(lo, hi)}
            self.assertEqual(actual, occupied_a - occupied_b)
            self.assertEqual(dependencies.elapsed(remaining) + dependencies.overlap(dependencies.union(a), dependencies.union(b)), len(occupied_a))


if __name__ == "__main__":
    unittest.main()

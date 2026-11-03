"""Check proportions after encoding and the game's 16:9 screen mapping."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "questions"))
import sdi


@unittest.skipUnless(shutil.which("ffmpeg"), "picture encoder requires FFmpeg")
class PictureAspectTest(unittest.TestCase):
    def test_portrait_square_and_wide_shapes_keep_display_proportions(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            sizes = [(90, 120), (120, 120), (160, 90)]
            images = []
            for i, (width, height) in enumerate(sizes):
                p = root / f"shape{i}.ppm"
                p.write_bytes(f"P6\n{width} {height}\n255\n".encode() + bytes([255]) * (width * height * 3))
                images.append(p)
            encoded = root / "pictures.m1v"
            encoded.write_bytes(b"".join(sdi.encode_frames(images)))
            rgb = subprocess.run(["ffmpeg", "-v", "error", "-i", str(encoded),
                                  "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                                 check=True, capture_output=True).stdout
            frame_size = sdi.WIDTH * sdi.HEIGHT * 3
            self.assertEqual(len(rgb), len(sizes) * frame_size)
            pixel_aspect = (16 / 9) / (sdi.WIDTH / sdi.HEIGHT)
            for i, (width, height) in enumerate(sizes):
                frame = rgb[i * frame_size:(i + 1) * frame_size]
                white = [p for p in range(sdi.WIDTH * sdi.HEIGHT) if frame[p * 3] > 200]
                xs = [p % sdi.WIDTH for p in white]
                ys = [p // sdi.WIDTH for p in white]
                ratio = (max(xs) - min(xs) + 1) / (max(ys) - min(ys) + 1) * pixel_aspect
                with self.subTest(size=(width, height)):
                    self.assertAlmostEqual(ratio, width / height, delta=0.01)


if __name__ == "__main__":
    unittest.main()

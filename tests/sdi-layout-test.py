import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("sdi", Path(__file__).resolve().parents[1]/"tools/inspect/inspect-sdi.py")
sdi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdi)


def fixture():
    data = bytearray(8192)
    for offset, value in {12:2560,16:640,20:368,24:640*368,28:29970,
                          36:80,40:2048,44:2048,48:8192,52:3}.items():
        struct.pack_into(">I", data, offset, value)
    data[64:72] = b"SDIVIDEO"
    struct.pack_into(">3I", data,80,4096,6144,6144)
    data[2048:2052] = b"\0\0\1\xba"
    data[4096:4100] = b"\0\0\1\xb3"
    data[4100:4103] = ((640 << 12)|368).to_bytes(3,"big")
    return data


class Layout(unittest.TestCase):
    def test_valid_duplicate_offsets(self):
        report=sdi.inspect_sdi(fixture())
        self.assertEqual(report["mpeg_sequence_dimensions"],[640,368])
        self.assertEqual(report["repeated_offsets"],1)

    def test_boundaries(self):
        for offset,value in ((16,0),(20,8193),(36,2049),(40,8192),(44,4096),
                             (48,8191),(52,0),(52,0xffffffff),(80,8192),(84,2047)):
            with self.subTest(offset=offset,value=value):
                data=fixture();struct.pack_into(">I",data,offset,value)
                with self.assertRaises(ValueError):sdi.inspect_sdi(data)

    def test_missing_header_or_pack(self):
        for offset in (64,2048):
            data=fixture();data[offset]=0xff
            with self.assertRaises(ValueError):sdi.inspect_sdi(data)
        with self.assertRaises(ValueError):sdi.inspect_sdi(bytes(79))


unittest.main()

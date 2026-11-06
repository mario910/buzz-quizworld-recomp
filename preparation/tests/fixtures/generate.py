"""Freeze arbitrary synthetic containers with the existing Python reference.

Developer-only regeneration; native builds/tests never invoke Python. No retail
content or real SELF/firmware keys are used. The fixture keys are byte sequences.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_ps3_import import (certified, pup_bytes, pkg_bytes, tar_bytes,
                            ROOT_KEY, ROOT_IV, PUP_KEY, NP_KEY, NP_FREE)
from test_setup_pipeline import sfo, make_iso

OUT = Path(__file__).parent
keys = dict(format=1, roots=[dict(category=c, self_type=t, revision=7,
                                 erk=ROOT_KEY.hex(), riv=ROOT_IV.hex())
                            for c, t in ((1, 4), (1, 8), (3, 0))],
            named=dict(pup_hmac=PUP_KEY.hex(), np_klic_key=NP_KEY.hex(), np_klic_free=NP_FREE.hex()))
(OUT / 'test-keys.json').write_text(json.dumps(keys, indent=2) + '\n', encoding='utf-8')
for compressed in (True, False):
    for signing in (2, 3, 6):
        wire, plain = certified([b'owned native preparation payload'], compressed=compressed, signing=signing)
        name = f'self-{int(compressed)}-{signing}'
        (OUT / f'{name}.fixture').write_bytes(wire)
        (OUT / f'{name}-expected.fixture').write_bytes(plain)
wire, plain = certified([b'owned free-license disc-update payload'], npdrm=True)
(OUT / 'npdrm.fixture').write_bytes(wire)
(OUT / 'npdrm-expected.fixture').write_bytes(plain)
(OUT / 'update-certified.fixture').write_bytes(pkg_bytes([
    ('PARAM.SFO', sfo()), ('USRDIR/EBOOT.BIN', wire),
    ('USRDIR/Patched/owned.dat', b'owned update content')]))

# Unsigned SELF exercises the existing segment table rather than encrypted metadata.
wire = bytearray(wire)
payload = zlib.compress(b'owned free-license disc-update payload')
struct.pack_into('>H', wire, 8, 0x8000)
struct.pack_into('>QQIIQ', wire, 0x190, 0x1000, len(payload), 2, 0, 2)
struct.pack_into('>QQIIQ', wire, 0x1b0, 0x1000, len(zlib.compress(b'owne')), 2, 0, 2)
# The second header aliases the LOAD section; reference unsigned reader expects
# its own four-byte segment, so use a separate stream and preserve alias bytes.
extra = zlib.compress(b'owne')
struct.pack_into('>Q', wire, 0x1b0, 0x1000 + len(payload))
wire = wire[:0x1000] + payload + extra
(OUT / 'unsigned.fixture').write_bytes(wire)
(OUT / 'unsigned-expected.fixture').write_bytes(plain)

firmware = [('dev_flash/data/font/test.TTF', b'owned font fixture'),
            ('dev_flash/sys/external/flashATRAC.pic', b'owned module fixture')]
inner = tar_bytes(firmware)
package, _ = certified([b'header', b'content header', inner], category=3)
(OUT / 'firmware.fixture').write_bytes(pup_bytes(tar_bytes([('dev_flash_000.tar.aa.test', package)])))
(OUT / 'firmware-tar.fixture').write_bytes(inner)
for name in ('../escape', 'dev_flash/../escape'):
    (OUT / ('unsafe-' + str(len(name)) + '.fixture')).write_bytes(tar_bytes([(name, b'unsafe')]))
link = tarfile.TarInfo()
link.type, link.linkname = tarfile.SYMTYPE, '../../outside'
(OUT / 'link-tar.fixture').write_bytes(tar_bytes([('dev_flash/link', link)]))
(OUT / 'duplicate-tar.fixture').write_bytes(tar_bytes([('dev_flash/a', b'a'), ('dev_flash/A', b'b')]))
files = [('PARAM.SFO', sfo()), ('USRDIR/EBOOT.BIN', b'owned update executable'),
         ('USRDIR/Patched/test.dat', bytes(range(255)) * 5000)]
for debug in (True, False):
    (OUT / f'update-{int(debug)}.fixture').write_bytes(pkg_bytes(files, debug=debug))
(OUT / 'sfo.fixture').write_bytes(sfo())
for udf, joliet in ((False, False), (False, True), (True, False), (True, True)):
    make_iso(OUT / f'iso-{int(udf)}-{int(joliet)}.fixture', udf=udf, joliet=joliet)
make_iso(OUT / 'iso-wrong-title.fixture', title='BCES00367')

# Small big-endian PPC64 executable: executable/data loads, entry OPD and two
# functions, a proc_prx_param record and a two-entry firmware import library.
elf = bytearray(0x600)
elf[:7] = b'\x7fELF\x02\x02\x01'
struct.pack_into('>HHIQQQI6H', elf, 16, 2, 21, 1, 0x11000, 64, 0x500, 0, 64, 56, 3, 64, 1, 0)
struct.pack_into('>II6Q', elf, 64, 1, 5, 0x200, 0x10000, 0x10000, 0x60, 0x80, 16)
struct.pack_into('>II6Q', elf, 120, 1, 6, 0x300, 0x11000, 0x11000, 0x200, 0x200, 16)
struct.pack_into('>II6Q', elf, 176, 0x60000002, 4, 0x340, 0x11040, 0x11040, 32, 32, 4)
struct.pack_into('>I', elf, 0x200, 0x4e800020)
struct.pack_into('>I', elf, 0x220, 0x4e800020)
struct.pack_into('>4I', elf, 0x300, 0x10000, 0x11180, 0x10020, 0x11180)
struct.pack_into('>8I', elf, 0x340, 32, 0x1b434cec, 1, 0, 0, 0, 0x11080, 0x110a0)
elf[0x380] = 32
struct.pack_into('>H', elf, 0x386, 2)
struct.pack_into('>3I', elf, 0x390, 0x11100, 0x11120, 0x11130)
elf[0x400:0x40d] = b'ownedLibrary\0'
struct.pack_into('>2I', elf, 0x420, 0x12345678, 0x9abcdef0)
struct.pack_into('>2I', elf, 0x430, 0x10000, 0x10020)
struct.pack_into('>II4QII2Q', elf, 0x500, 0, 1, 2, 0x11000, 0x300, 16, 0, 0, 8, 8)
(OUT / 'ppu-image.fixture').write_bytes(elf)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / 'EBOOT.elf'
    path.write_bytes(elf)
    subprocess.run([sys.executable, str(ROOT / 'ps3recomp/tools/ppu_loader.py'), str(path), '-o', folder], check=True)
    reference = {name: json.loads((Path(folder) / f'EBOOT.{name}.json').read_text())
                 for name in ('functions', 'image', 'imports', 'loader')}
    (OUT / 'ppu-analysis.json').write_text(json.dumps(reference, indent=2) + '\n', encoding='utf-8')

sys.path.insert(0, str(ROOT / 'ps3recomp/tools'))
from extract_spu_images import find_spu_images
from spu_elf import fnv1a64

# Image fingerprint includes a non-load segment beyond the section table.
spu = bytearray(0x120)
spu[:7] = b'\x7fELF\x01\x02\x01'
struct.pack_into('>HHIIIII6H', spu, 16, 2, 23, 1, 0x10, 52, 0xa0, 0, 52, 32, 2, 40, 2, 0)
struct.pack_into('>8I', spu, 52, 1, 0x80, 0x10, 0x10, 8, 16, 5, 16)
struct.pack_into('>8I', spu, 84, 4, 0x110, 0, 0, 16, 16, 0, 4)
struct.pack_into('>2I', spu, 0x80, 0x00200000, 0x35000000)
struct.pack_into('>10I', spu, 0xa0, 0, 1, 6, 0x10, 0x80, 8, 0, 0, 4, 0)
struct.pack_into('>10I', spu, 0xc8, 0, 2, 0, 0, 0x90, 16, 0, 0, 4, 16)
spu[0x110:0x120] = b'owned note bytes'
bad = bytearray(spu)
struct.pack_into('>I', bad, 60, 0xffffffff)  # invalid local-store address
container = b'owned-prefix' + spu + b'gap' + bad + b'gap' + spu
(OUT / 'spu-container.fixture').write_bytes(container)
reference = [dict(name=f'spu_{i:04d}_at_{offset:08X}.elf', offset=offset, size=size, entry=0x10,
                  fnv=hex(fnv1a64(container[offset:offset + size])),
                  sha256=hashlib.sha256(container[offset:offset + size]).hexdigest())
             for i, (offset, size) in enumerate(find_spu_images(container))]
(OUT / 'spu-images.json').write_text(json.dumps(reference, indent=2) + '\n', encoding='utf-8')

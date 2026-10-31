"""Extract files from a fake-signed (fPKG) PS4 package, e.g. a game dump the
user owns, for question import. Python port of the documented LibOrbisPkg
algorithms (maxton/LibOrbisPkg, MIT): passcode-derived EKPFS, XTS-AES outer
PFS, PFSC-compressed inner PFS. Retail (non-fake) packages are not supported.

  python -I ps4pkg.py list    <file.pkg> [--passcode 000...0]
  python -I ps4pkg.py extract <file.pkg> <out dir> [--filter regex]

Needs the `cryptography` package.
"""
import argparse
import hashlib
import hmac
import re
import struct
import sys
import zlib
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

XTS_SECTOR = 0x1000


def compute_keys(content_id, passcode, index):
    data = (hashlib.sha256(struct.pack(">I", index)).digest()
            + hashlib.sha256(content_id.encode().ljust(48, b"\0")).digest()
            + passcode.encode())
    return hashlib.sha256(data).digest()


def pfs_enc_keys(ekpfs, seed, new_crypt):
    key = hmac.new(ekpfs, seed, hashlib.sha256).digest() if new_crypt else ekpfs
    k = hmac.new(key, struct.pack("<I", 1) + seed, hashlib.sha256).digest()
    return k[:16], k[16:]  # tweak, data


class FileReader:
    def __init__(self, f, base, size):
        self.f, self.base, self.size = f, base, size

    def read(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)


class XtsReader:
    """Decrypts sectors >= start_sector; sector number is the XTS tweak."""

    def __init__(self, inner, tweak_key, data_key, start_sector):
        self.inner, self.start = inner, start_sector
        self.key = data_key + tweak_key
        self.size = inner.size
        self._cache = (None, None)

    def _sector(self, n):
        if self._cache[0] == n:
            return self._cache[1]
        raw = self.inner.read(n * XTS_SECTOR, XTS_SECTOR)
        if n >= self.start:
            d = Cipher(algorithms.AES(self.key), modes.XTS(struct.pack("<QQ", n, 0))).decryptor()
            raw = d.update(raw) + d.finalize()
        self._cache = (n, raw)
        return raw

    def read(self, off, n):
        out = bytearray()
        while n > 0:
            s, o = divmod(off, XTS_SECTOR)
            chunk = self._sector(s)[o:o + n]
            out += chunk
            off += len(chunk)
            n -= len(chunk)
        return bytes(out)


class BytesReader:
    def __init__(self, data):
        self.data, self.size = data, len(data)

    def read(self, off, n):
        return self.data[off:off + n]


class SpanReader:
    """A file inside a PFS: list of blocks (contiguous runs) of block_size."""

    def __init__(self, inner, blocks, block_size, size):
        self.inner, self.blocks, self.bs, self.size = inner, blocks, block_size, size

    def read(self, off, n):
        out = bytearray()
        n = min(n, self.size - off)
        while n > 0:
            b, o = divmod(off, self.bs)
            take = min(n, self.bs - o)
            out += self.inner.read(self.blocks[b] * self.bs + o, take)
            off += take
            n -= take
        return bytes(out)


class PfscReader:
    MAGIC = 0x43534650  # 'PFSC'

    def __init__(self, inner):
        self.inner = inner
        magic, unk4, _, bs, bs2, offs, _, length = struct.unpack("<iiiiqqQq", inner.read(0, 0x30))
        if magic != self.MAGIC or unk4 != 0 or bs != bs2:
            raise ValueError("not a PFSC image")
        self.bs, self.size = bs, length
        n = length // bs2
        self.map = struct.unpack(f"<{n + 1}q", inner.read(offs, 8 * (n + 1)))
        self._cache = (None, None)

    def _block(self, i):
        if self._cache[0] == i:
            return self._cache[1]
        a, b = self.map[i], self.map[i + 1]
        if b - a == self.bs:
            data = self.inner.read(a, self.bs)
        elif b - a > self.bs:
            data = bytes(self.bs)
        else:
            data = zlib.decompress(self.inner.read(a + 2, b - a - 2), -15).ljust(self.bs, b"\0")
        self._cache = (i, data)
        return data

    def read(self, off, n):
        out = bytearray()
        n = min(n, self.size - off)
        while n > 0:
            i, o = divmod(off, self.bs)
            chunk = self._block(i)[o:o + n]
            out += chunk
            off += len(chunk)
            n -= len(chunk)
        return bytes(out)


# mode, nlink, flags, size, size_c, (4 times, 4 nsec, uid, gid, 2 unk), blocks: 0x64 bytes
INODE_HDR = struct.Struct("<HHIqq32x16x8x16xI")


class Pfs:
    def __init__(self, reader, ekpfs=None, new_crypt=False):
        h = reader.read(0, 0x400)
        version, magic = struct.unpack_from("<qq", h, 0)
        if version != 1 or magic != 20130315:
            raise ValueError("bad PFS superblock")
        mode, = struct.unpack_from("<H", h, 0x1C)
        self.bs, = struct.unpack_from("<I", h, 0x20)
        dinode_count, _, dinode_blocks = struct.unpack_from("<qqq", h, 0x30)
        self.signed = bool(mode & 1)
        if mode & 4:
            if ekpfs is None:
                raise ValueError("encrypted PFS needs EKPFS")
            tweak, data = pfs_enc_keys(ekpfs, h[0x370:0x380], new_crypt)
            reader = XtsReader(reader, tweak, data, self.bs // XTS_SECTOR)
        self.r = reader
        size = 0x2C8 if self.signed else 0xA8
        per = self.bs // size
        self.inodes = []
        for i in range(dinode_blocks):
            blk = reader.read(self.bs * (1 + i), self.bs)
            for j in range(per):
                if len(self.inodes) >= dinode_count:
                    break
                self.inodes.append(self._inode(blk[j * size:(j + 1) * size]))
        self.files = {}
        self._walk(0, "")
        if not any(p == "uroot" or p.startswith("uroot/") for p in self.files) and "uroot" not in self._dirs:
            raise ValueError("no uroot: wrong key?")

    def _inode(self, b):
        mode, nlink, flags, size, size_c, blocks = INODE_HDR.unpack_from(b, 0)
        if self.signed:
            db = [struct.unpack_from("<i", b, 0x64 + 36 * k + 32)[0] for k in range(12)]
            ib = [struct.unpack_from("<i", b, 0x64 + 36 * (12 + k) + 32)[0] for k in range(5)]
        else:
            db = list(struct.unpack_from("<12i", b, 0x64))
            ib = list(struct.unpack_from("<5i", b, 0x94))
        return dict(mode=mode, flags=flags, size=size, size_c=size_c, blocks=blocks, db=db, ib=ib)

    def _block_list(self, ino):
        n = ino["blocks"]
        if n <= 1 or ino["db"][1] == -1 or not self.signed:
            return list(range(ino["db"][0], ino["db"][0] + max(n, 1)))
        blocks = ino["db"][:min(12, n)]
        per = self.bs // 36

        def sig_block(b, count):
            raw = self.r.read(b * self.bs, count * 36)
            return [struct.unpack_from("<i", raw, k * 36 + 32)[0] for k in range(count)]
        rem = n - len(blocks)
        if rem > 0:
            blocks += sig_block(ino["ib"][0], min(rem, per))
            rem = n - len(blocks)
        if rem > 0:
            for ind in sig_block(ino["ib"][1], (rem + per - 1) // per):
                blocks += sig_block(ind, min(rem, per))
                rem = n - len(blocks)
        return blocks

    _dirs = set()

    def _walk(self, ino_no, path):
        self._dirs = self._dirs | {path}
        ino = self.inodes[ino_no]
        for b in range(ino["db"][0], ino["db"][0] + ino["blocks"]):
            blk = self.r.read(b * self.bs, self.bs)
            pos = 0
            while pos + 16 <= len(blk):
                num, typ, nlen, ent = struct.unpack_from("<IiiI", blk, pos)
                if ent == 0:
                    break
                name = blk[pos + 16:pos + 16 + nlen].decode("ascii", "replace")
                full = f"{path}/{name}" if path else name
                if typ == 2:
                    self.files[full] = num
                elif typ == 3:
                    self._walk(num, full)
                pos += ent

    def open(self, path):
        ino = self.inodes[self.files[path]]
        # "size" is the stored size; compressed files are PFSC streams whose
        # header gives the logical length (also in size_c).
        f = SpanReader(self.r, self._block_list(ino), self.bs, ino["size"])
        if ino["flags"] & 1:
            f = PfscReader(f)
        return f


def open_pkg(path, passcode="0" * 32):
    f = open(path, "rb")
    h = f.read(0x1000)
    if h[:4] != b"\x7fCNT":
        raise ValueError("not a PS4 PKG")
    content_id = h[0x40:0x64].decode()
    pfs_flags, pfs_off, pfs_size = struct.unpack_from(">QQQ", h, 0x408)
    ekpfs = compute_keys(content_id, passcode, 1)
    outer = Pfs(FileReader(f, pfs_off, pfs_size), ekpfs, bool(pfs_flags & 0x2000000000000000))
    inner = Pfs(outer.open("uroot/pfs_image.dat"))
    return content_id, inner


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["list", "extract"])
    ap.add_argument("pkg")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--passcode", default="0" * 32)
    ap.add_argument("--filter", default=".")
    a = ap.parse_args(argv)
    cid, pfs = open_pkg(a.pkg, a.passcode)
    pat = re.compile(a.filter, re.I)
    files = sorted(p for p in pfs.files if p.startswith("uroot/") and pat.search(p))
    if a.cmd == "list":
        print(cid)
        for p in files:
            ino = pfs.inodes[pfs.files[p]]
            print(f"{ino['size']:>12} {p[6:]}")
        return
    out = Path(a.out)
    for p in files:
        src = pfs.open(p)
        dst = out / p[6:]
        dst.parent.mkdir(parents=True, exist_ok=True)
        with open(dst, "wb") as w:
            for off in range(0, src.size, 1 << 22):
                w.write(src.read(off, min(1 << 22, src.size - off)))
        print(p[6:], src.size)


if __name__ == "__main__":
    main(sys.argv[1:])

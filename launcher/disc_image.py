"""Read-only PS3 image reader with optional sector decryption.

Some PS3 hybrid images have UDF file-set descriptors pycdlib cannot parse,
while their independent ISO9660 directory tree is intact. In that case,
retry the ISO filesystem using the pinned library version.
The caller must still validate SFO, executable hashes, paths and bounds.
"""
from bisect import bisect_right
import io
import os
import struct

import pycdlib
from pycdlib.pycdlibexception import PyCdlibInvalidISO

from .ps3_crypto import aes


SECTOR_SIZE = 2048
PS3_MARKER = b"PlayStation3\0\0\0\0"


def encrypted_regions(header, size):
    """Read PS3 sector-0 boundaries as half-open encrypted LBA ranges.

    The big-endian table interleaves inclusive ends of plaintext ranges
    with exclusive ends of encrypted ranges. See PS3Dec/src/PS3Dec.c.
    """
    count = 2 * int.from_bytes(header[:4], "big") - 1
    if size % SECTOR_SIZE or count < 3 or 12 + 4 * count > len(header):
        return ()
    boundaries = struct.unpack_from(f">{count}I", header, 12)
    ends = [boundary + (index % 2 == 0) for index, boundary in enumerate(boundaries)]
    if ends[-1] != size // SECTOR_SIZE or any(start >= end for start, end in zip(ends, ends[1:])):
        return ()
    return tuple(zip(ends[::2], ends[1::2]))


class _SectorReader(io.RawIOBase):
    def __init__(self, source, size, regions, key):
        self.source = source
        self.size = size
        self.regions = regions
        self.ends = tuple(end for _, end in regions)
        self.key = key
        self.cipher = aes()

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.source.tell()

    def seek(self, offset, whence=os.SEEK_SET):
        return self.source.seek(offset, whence)

    def readinto(self, buffer):
        position = self.tell()
        end = min(position + len(buffer), self.size)
        if end <= position:
            return 0
        first = position // SECTOR_SIZE
        stop = (end + SECTOR_SIZE - 1) // SECTOR_SIZE
        region = bisect_right(self.ends, first)
        output = memoryview(buffer)[:end - position]
        if region == len(self.regions) or self.regions[region][0] >= stop:
            return self.source.readinto(output)

        aligned = first * SECTOR_SIZE
        length = (stop - first) * SECTOR_SIZE
        self.source.seek(aligned)
        data = bytearray(self.source.read(length))
        if len(data) != length:
            raise ValueError("Truncated PS3 disc sector while decrypting the ISO.")
        for start, finish in self.regions[region:]:
            if start >= stop:
                break
            for lba in range(max(first, start), min(stop, finish)):
                offset = (lba - first) * SECTOR_SIZE
                sector = memoryview(data)[offset:offset + SECTOR_SIZE]
                cipher = self.cipher.new(self.key, self.cipher.MODE_CBC, iv=lba.to_bytes(16, "big"))
                sector[:] = cipher.decrypt(sector)
        self.source.seek(end)
        offset = position - aligned
        output[:] = memoryview(data)[offset:offset + len(output)]
        return len(output)


class _ISO9660Only(pycdlib.PyCdlib):
    # Private hooks are intentionally limited to the pinned pycdlib 1.22.0.
    # Synthetic hybrid-image regression tests cover this compatibility shim.
    def _parse_udf_descriptors(self):
        self._has_udf = False

    def _walk_udf_directories(self, extent_to_inode):
        return None


class DiscImage:
    def __init__(self):
        self.source = None
        self.stream = None
        self.image = None
        self.fallback_reason = ""
        self.has_ps3_regions = False

    def open(self, path, *, disc_key=None):
        self.source = open(path, "rb")
        try:
            self.stream = self.source
            size = os.fstat(self.source.fileno()).st_size
            regions = encrypted_regions(self.source.read(SECTOR_SIZE), size)
            self.has_ps3_regions = bool(regions) and self.source.read(len(PS3_MARKER)) == PS3_MARKER
            self.source.seek(0)
            if disc_key is not None:
                if not regions:
                    raise ValueError("Invalid PS3 sector table for the matched disc image.")
                self.stream = io.BufferedReader(_SectorReader(self.source, size, regions, disc_key), 64 * 1024)
            image = pycdlib.PyCdlib()
            try:
                image.open_fp(self.stream)
            except PyCdlibInvalidISO as exc:
                if str(exc) != "UDF File Set Tag identifier not 256":
                    raise
                self.stream.seek(0)
                image = _ISO9660Only()
                image.open_fp(self.stream)
                self.fallback_reason = "UDF file-set unsupported; reading the independent ISO9660/Joliet filesystem."
            self.image = image
        except BaseException:
            self.close()
            raise

    def close(self):
        try:
            if self.image is not None:
                self.image.close()
        finally:
            try:
                if self.stream is not None and self.stream is not self.source:
                    self.stream.close()
            finally:
                if self.source is not None:
                    self.source.close()

    def __getattr__(self, name):
        return getattr(self.image, name)

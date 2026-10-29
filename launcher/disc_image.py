"""Read-only image reader with a narrowly scoped ISO9660 bridge fallback.

Some PS3 hybrid images have UDF file-set descriptors pycdlib cannot parse,
while their independent ISO9660 directory tree is intact. No bytes or keys
are patched: retry ONLY the ISO filesystem using the pinned library version.
The caller must still validate SFO, executable hashes, paths and bounds.
"""
import pycdlib
from pycdlib.pycdlibexception import PyCdlibInvalidISO


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
        self.image = None
        self.fallback_reason = ""

    def open(self, path):
        self.source = open(path, "rb")
        try:
            image = pycdlib.PyCdlib()
            try:
                image.open_fp(self.source)
            except PyCdlibInvalidISO as exc:
                if str(exc) != "UDF File Set Tag identifier not 256":
                    raise
                self.source.seek(0)
                image = _ISO9660Only()
                image.open_fp(self.source)
                self.fallback_reason = "UDF file-set unsupported; reading the independent ISO9660/Joliet filesystem."
            self.image = image
        except BaseException:
            self.source.close()
            self.source = None
            raise

    def close(self):
        try:
            if self.image is not None:
                self.image.close()
        finally:
            if self.source is not None:
                self.source.close()

    def __getattr__(self, name):
        return getattr(self.image, name)

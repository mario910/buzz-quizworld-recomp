"""In-process PS3 Certified File reader. No emulator, downloads or embedded keys.

Implemented from the public container descriptions:
https://www.psdevwiki.com/ps3/Certified_File
https://www.psdevwiki.com/ps3/SELF_-_SPRX
Only extraction is supported; this is not a signature/console activation tool.
"""
from __future__ import annotations

import configparser
import hashlib
import hmac
import json
from pathlib import Path
import re
import struct
import zlib

MAX_BINARY = 256 * 1024**2
MAX_HEADER = 16 * 1024**2


def part(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError("Truncated or invalid PS3 container range.")
    return data[offset:offset + size]


def unpack(fmt, data, offset=0):
    return struct.unpack(fmt, part(data, offset, struct.calcsize(fmt)))


def aes():
    try:
        from Crypto.Cipher import AES
        return AES
    except ImportError:
        raise ValueError("Install the preparation dependencies (pycryptodome); run scripts/Setup-Buzz.ps1.") from None


def ctr(data, key, iv):
    return aes().new(key, aes().MODE_CTR, nonce=b"", initial_value=int.from_bytes(iv, "big")).decrypt(data)


def inflate(data, limit):
    decoder = zlib.decompressobj()
    try:
        plain = decoder.decompress(data, limit + 1)
    except zlib.error:
        raise ValueError("Invalid compressed PS3 section.") from None
    if len(plain) > limit or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("Compressed PS3 section is truncated or exceeds its output limit.")
    return plain


class KeyStore:
    """Private JSON or scetool-format key file; never include values in errors."""

    def __init__(self, path=""):
        self.roots, self.named = [], {}
        if not path:
            return
        path = Path(path)
        if not path.is_file() or path.stat().st_size > 2 * 1024**2:
            raise ValueError("Decryption key file is missing or oversized.")
        try:
            source = path.read_text(encoding="utf-8-sig")
            # JSON uses an object; scetool uses INI sections.
            if source.lstrip().startswith("{"):
                document = json.loads(source)
                if document.get("format") != 1:
                    raise ValueError()
                for entry in document.get("roots", []):
                    self.add_root(entry)
                for name, value in document.get("named", {}).items():
                    self.named[name.lower()] = self.hex(value)
            else:
                types = {"LV0": 1, "LV1": 2, "LV2": 3, "APP": 4, "ISO": 5, "LDR": 6, "UNK7": 7, "NPDRM": 8}
                # scetool key files legitimately repeat section names (pkg,
                # appldr) for different revisions. Parse each block separately.
                blocks = re.split(r"(?m)^\s*\[([^\]\r\n]+)\]\s*$", source)
                if len(blocks) < 3:
                    raise ValueError()
                for section, body in zip(blocks[1::2], blocks[2::2]):
                    ini = configparser.ConfigParser(interpolation=None, strict=True)
                    ini.read_string("[" + section + "]\n" + body)
                    entry = ini[section]
                    if entry.get("type", "").upper() in ("SELF", "PKG") and "erk" in entry and "riv" in entry:
                        self.add_root(dict(category=1 if entry["type"].upper() == "SELF" else 3,
                                           self_type=types.get(entry.get("self_type", "").upper(), 0),
                                           revision=int(entry.get("revision", "0"), 16), erk=entry["erk"], riv=entry["riv"]))
                    elif "key" in entry:
                        self.named[section.lower()] = self.hex(entry["key"])
        except (ValueError, KeyError, TypeError, AttributeError, configparser.Error, UnicodeError):
            raise ValueError("Invalid decryption key file. See docs/game-import.md for the supported formats.") from None

    @staticmethod
    def hex(value):
        return bytes.fromhex(value)

    def add_root(self, entry):
        category, kind, revision = int(entry["category"]), int(entry.get("self_type", 0)), int(entry.get("revision", 0))
        key, iv = self.hex(entry["erk"]), self.hex(entry["riv"])
        if category not in (1, 3) or not 0 <= kind <= 8 or not 0 <= revision <= 0xffff or len(key) != 32 or len(iv) != 16:
            raise ValueError()
        self.roots.append((category, kind, revision, key, iv))

    def named_key(self, name, size):
        value = self.named.get(name.lower())
        if value is None or len(value) != size:
            raise ValueError(f"Private key file is missing {name} ({size} bytes).")
        return value

    def candidates(self, category, kind, revision):
        # Some old key files have no revision, or several firmware versions for
        # one revision. Padding and metadata bounds validate the selected root.
        matching = [entry for entry in self.roots if entry[:2] == (category, kind)]
        return sorted(matching, key=lambda entry: entry[2] != revision)


class CertifiedFile:
    def __init__(self, data, keys, cancel=lambda: None):
        self.data, self.cancel = data, cancel
        magic, version, self.revision, self.category, extended, self.header_size, self.data_size = unpack(
            ">4sIHHIQQ", data)
        if magic != b"SCE\0" or version != 2 or self.category not in (1, 3):
            raise ValueError("Unsupported PS3 Certified File (expected SELF or firmware package).")
        if not 0x20 <= self.header_size <= min(len(data), MAX_HEADER):
            raise ValueError("Invalid PS3 Certified File header size.")
        self.kind, self.sections, self.attributes = 0, [], b""
        if self.category == 1:
            (header_version, self.app_offset, self.elf_offset, self.phdr_offset,
             self.shdr_offset, self.segment_offset, _, self.control_offset,
             self.control_size, _) = unpack(">10Q", data, 0x20)
            if header_version != 3:
                raise ValueError("Unsupported SELF extended header.")
            _, _, self.kind, _ = unpack(">QIIQ", data, self.app_offset)
        if self.revision & 0x8000:
            if self.category != 1:
                raise ValueError("Unsupported unsigned firmware package.")
            return
        root_offset = extended + 0x20
        if root_offset + 0x60 > self.header_size:
            raise ValueError("Invalid PS3 metadata offset.")
        encrypted_root = part(data, root_offset, 0x40)
        if self.kind == 8:
            self.check_free_npdrm()
            klic = aes().new(keys.named_key("np_klic_key", 16), aes().MODE_ECB).decrypt(keys.named_key("np_klic_free", 16))
            encrypted_root = aes().new(klic, aes().MODE_CBC, iv=bytes(16)).decrypt(encrypted_root)
        for _, _, _, key, iv in keys.candidates(self.category, self.kind, self.revision):
            cancel()
            root = aes().new(key, aes().MODE_CBC, iv=iv).decrypt(encrypted_root)
            if root[16:32] == bytes(16) and root[48:64] == bytes(16):
                metadata = ctr(part(data, root_offset + 64, self.header_size - root_offset - 64), root[:16], root[32:48])
                self.read_metadata(metadata)
                break
        else:
            raise ValueError(f"No matching decryption key: category {self.category}, SELF type {self.kind}, revision {self.revision:#06x}. Select a private key file in Prepare.")

    def check_free_npdrm(self):
        controls = part(self.data, self.control_offset, self.control_size)
        at = 0
        while at < len(controls):
            kind, size, following = unpack(">IIQ", controls, at)
            if size < 16:
                raise ValueError("Invalid SELF control record.")
            record = part(controls, at, size)
            if kind == 3:
                magic, _, license_type = unpack(">4sII", record, 16)
                if magic != b"NPD\0" or license_type != 3:
                    raise ValueError("This importer supports disc games and free-license disc updates, not account-licensed NPDRM executables.")
                return
            at += size
            if not following:
                break
        raise ValueError("NPDRM SELF has no readable license information.")

    def read_metadata(self, metadata):
        _, _, count, key_count, optional_size, _ = unpack(">QIIIIQ", metadata)
        if not 0 < count <= 4096 or key_count > 65536:
            raise ValueError("Invalid decrypted PS3 section count.")
        self.attributes = part(metadata, 32 + count * 48, key_count * 16)
        part(metadata, 32 + count * 48 + key_count * 16, optional_size)
        for i in range(count):
            section = unpack(">QQ8I", metadata, 32 + i * 48)
            offset, size, _, _, signing, digest, encryption, key, iv, compression = section
            if offset < self.header_size or size > MAX_BINARY:
                raise ValueError("Invalid PS3 section location/size.")
            part(self.data, offset, size)
            if encryption not in (1, 3) or compression not in (1, 2) or signing not in (2, 3, 6):
                raise ValueError("Unsupported PS3 section encryption, compression or digest.")
            if encryption == 3:
                self.attribute(key, 16)
                self.attribute(iv, 16)
            self.attribute(digest, 96 if signing == 2 else 64 if signing == 6 else 32)
            self.sections.append(section)

    def attribute(self, index, size):
        return part(self.attributes, index * 16, size)

    def section_data(self, section, limit=MAX_BINARY):
        self.cancel()
        offset, size, _, _, signing, digest, encryption, key, iv, compression = section
        data = part(self.data, offset, size)
        if encryption == 3:
            data = ctr(data, self.attribute(key, 16), self.attribute(iv, 16))
        cert = self.attribute(digest, 96 if signing == 2 else 64 if signing == 6 else 32)
        if signing == 2:
            actual = hmac.digest(cert[32:96], data, "sha1")
        elif signing == 6:
            actual = hmac.digest(cert[32:64], data, "sha256")
        else:
            actual = hashlib.sha1(data).digest()
        if not hmac.compare_digest(actual, cert[:len(actual)]):
            raise ValueError("PS3 section digest mismatch; input is corrupt or keys do not match.")
        if compression == 2:
            data = inflate(data, limit)
        if len(data) > limit:
            raise ValueError("PS3 section exceeds its output limit.")
        self.cancel()
        return data

    def elf(self):
        if self.category != 1:
            raise ValueError("Expected a SELF executable.")
        header = part(self.data, self.elf_offset, 64)
        if header[:7] != b"\x7fELF\x02\x02\x01":
            raise ValueError("Expected a PS3 big-endian ELF64 executable.")
        phoff, shoff = unpack(">QQ", header, 32)
        ehsize, phsize, phcount, shsize, shcount, _ = unpack(">6H", header, 52)
        if ehsize != 64 or phsize != 56 or phcount > 4096 or (shcount and shsize != 64):
            raise ValueError("Invalid embedded ELF header tables.")
        output = bytearray()
        def write(offset, block):
            if offset > MAX_BINARY or len(block) > MAX_BINARY - offset:
                raise ValueError("ELF exceeds the preparation size limit.")
            if offset + len(block) > len(output):
                output.extend(bytes(offset + len(block) - len(output)))
            output[offset:offset + len(block)] = block
        write(0, header)
        phdrs = part(self.data, self.phdr_offset, phsize * phcount)
        write(phoff, phdrs)
        if shcount:
            write(shoff, part(self.data, self.shdr_offset, shsize * shcount))
        found, covered = set(), []
        sections = self.sections
        if self.revision & 0x8000:
            sections = []
            for i in range(phcount):
                self.cancel()
                offset, size, compression, _, encryption = unpack(">QQIIQ", self.data, self.segment_offset + i * 32)
                if encryption not in (0, 2):
                    raise ValueError("Unsigned SELF contains an encrypted section.")
                sections.append((offset, size, 2, i, 0, 0, 1, 0, 0, compression))
        for section in sections:
            _, _, kind, index, *_ = section
            if kind != 2:
                continue
            if index >= phcount or index in found:
                raise ValueError("Invalid/duplicate ELF program section index.")
            found.add(index)
            _, _, offset, _, _, file_size, _, _ = unpack(">II6Q", phdrs, index * phsize)
            if file_size > MAX_BINARY:
                raise ValueError("ELF program exceeds the preparation size limit.")
            if self.revision & 0x8000:
                data = part(self.data, section[0], section[1])
                if section[-1] == 2:
                    data = inflate(data, file_size)
                elif section[-1] != 1:
                    raise ValueError("Unsupported unsigned SELF compression.")
            else:
                data = self.section_data(section, file_size)
            if len(data) != file_size:
                raise ValueError("ELF section size does not match its program header.")
            write(offset, data)
            covered.append((offset, offset + len(data)))
        for i in range(phcount):
            offset = unpack(">Q", phdrs, i * phsize + 8)[0]
            size = unpack(">Q", phdrs, i * phsize + 32)[0]
            # TLS and process parameters can point into an existing LOAD
            # segment and have no separate certification record.
            if size and i not in found and not any(start <= offset and offset + size <= end for start, end in covered):
                raise ValueError("SELF is missing an ELF program section.")
        return bytes(output)


def decrypt_elf(source, keys, cancel=lambda: None):
    source = Path(source)
    if not 0 < source.stat().st_size <= MAX_BINARY:
        raise ValueError("Executable exceeds the preparation size limit.")
    cancel()
    data = source.read_bytes()
    if data[:6] == b"\x7fELF\x02\x02":
        return data
    return CertifiedFile(data, keys, cancel).elf()

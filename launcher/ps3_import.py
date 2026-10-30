"""Bounded, cancellable firmware/update import into a private setup session.

Source inputs are read only. Archive paths are validated before writing, and
links, device files, duplicate names and Windows path aliases are rejected.
Format references and key-file instructions are in docs/game-import.md.
"""
from __future__ import annotations

import hashlib
import hmac
import importlib.util
import io
import os
from pathlib import Path
import shutil
import struct
import tarfile
import uuid

from .ps3_crypto import CertifiedFile, KeyStore, MAX_BINARY, part, unpack

MAX_FIRMWARE = 2 * 1024**3
MAX_FILES = 20000
CHUNK = 1024**2


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").digest()


def atomic_bytes(target, data, root):
    from .setup_pipeline import reject_links_below
    target, root = Path(target), Path(root)
    reject_links_below(root, target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".import-partial-" + uuid.uuid4().hex)
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def archive_path(name):
    from .setup_pipeline import safe_component
    if name.startswith("/") or "\\" in name:
        raise ValueError("Unsafe archive path.")
    pieces = name.rstrip("/").split("/")
    if pieces and pieces[0] == ".":
        pieces.pop(0)
    if not pieces or len(pieces) > 64:
        raise ValueError("Invalid archive path depth.")
    return Path(*(safe_component(piece) for piece in pieces))


def tar_entries(archive, cancel, *, prefix=None):
    seen, total = set(), 0
    for index, member in enumerate(archive):
        cancel()
        if index >= MAX_FILES:
            raise ValueError("Firmware archive exceeds its file-count limit.")
        # A TAR may include the root directory itself.
        if member.name in (".", "./") and member.isdir():
            continue
        path = archive_path(member.name)
        name = path.as_posix().casefold()
        if name in seen or not (member.isdir() or member.isfile()):
            raise ValueError("Duplicate path, link or special file in firmware archive.")
        seen.add(name)
        if not 0 <= member.size <= MAX_BINARY:
            raise ValueError("Firmware archive member exceeds its size limit.")
        total += member.size
        if total > MAX_FIRMWARE:
            raise ValueError("Firmware archive exceeds its expanded size limit.")
        if prefix is not None and path.parts[0] != prefix:
            raise ValueError("Firmware package writes outside dev_flash.")
        yield member, path


def firmware_archive(source, keys, cancel):
    """Read and authenticate the PUP header and update_files.tar segment."""
    source = Path(source)
    size = source.stat().st_size
    if not 0x50 <= size <= MAX_FIRMWARE:
        raise ValueError("Firmware PUP is truncated or oversized.")
    with source.open("rb") as stream:
        header = stream.read(48)
        magic, version, _, count, header_size, data_size = unpack(">8s5Q", header)
        if magic != b"SCEUF\0\0\0" or version != 1 or not 0 < count <= 1024:
            raise ValueError("Unsupported firmware PUP header.")
        if not 0x50 + count * 64 <= header_size <= min(size, 1024**2) or data_size > size - header_size:
            raise ValueError("Invalid firmware PUP header/data sizes.")
        stream.seek(0)
        header = stream.read(header_size)
        key = keys.named_key("pup_hmac", 64)
        digest_at = 48 + count * 64
        if not hmac.compare_digest(hmac.digest(key, header[:digest_at], "sha1"), part(header, digest_at, 20)):
            raise ValueError("Firmware PUP header digest mismatch.")
        hashes = {}
        for i in range(count):
            index, digest, _ = unpack(">Q20sI", header, 48 + count * 32 + i * 32)
            if index >= count or index in hashes:
                raise ValueError("Invalid firmware PUP digest table.")
            hashes[index] = digest
        regions, identities, selected = [], set(), None
        for i in range(count):
            entry, offset, length, algorithm, _ = unpack(">QQQII", header, 48 + i * 32)
            if entry in identities or offset < header_size or length > data_size or offset + length > header_size + data_size or algorithm != 0:
                raise ValueError("Invalid firmware PUP segment.")
            identities.add(entry)
            regions.append((offset, offset + length))
            if entry == 0x300:
                selected = i, offset, length
        regions.sort()
        if any(a[1] > b[0] for a, b in zip(regions, regions[1:])):
            raise ValueError("Overlapping firmware PUP segments.")
        if selected is None:
            raise ValueError("Firmware PUP has no update_files.tar; supply a full PS3 firmware PUP.")
        index, offset, length = selected
        if length > MAX_BINARY:
            raise ValueError("Firmware update archive exceeds the import limit.")
        stream.seek(offset)
        data, digest = bytearray(), hmac.new(key, digestmod="sha1")
        while len(data) < length:
            cancel()
            chunk = stream.read(min(CHUNK, length - len(data)))
            if not chunk:
                raise ValueError("Truncated firmware PUP segment.")
            data.extend(chunk)
            digest.update(chunk)
        if not hmac.compare_digest(digest.digest(), hashes[index]):
            raise ValueError("Firmware PUP update archive digest mismatch.")
        return bytes(data)


def extract_firmware(source, home, keys, log, cancel):
    from .setup_pipeline import reject_links_below, firmware_validator
    home = Path(home)
    reject_links_below(home, home / "dev_flash")
    data = firmware_archive(source, keys, cancel)
    try:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as outer:
            planned = [(member, path) for member, path in tar_entries(outer, cancel)
                       if member.isfile() and len(path.parts) == 1 and path.name.startswith("dev_flash_")]
            if not planned:
                raise ValueError("Firmware PUP contains no dev_flash packages.")
            # Keep incomplete output separate; a successful import is published
            # only after the module hash and fonts have passed validation.
            staging = home / ("firmware-import-" + uuid.uuid4().hex)
            staging.mkdir(parents=True)
            total, seen = 0, set()
            for index, (member, path) in enumerate(planned):
                cancel()
                log(f"Firmware: {index + 1}/{len(planned)} packages")
                with outer.extractfile(member) as stream:
                    package = CertifiedFile(stream.read(MAX_BINARY + 1), keys, cancel)
                if package.category != 3 or len(package.sections) != 3:
                    raise ValueError("Unsupported firmware package layout.")
                # System packages have a header, content header and TAR payload.
                payload = package.section_data(package.sections[2])
                with tarfile.open(fileobj=io.BytesIO(payload), mode="r:") as inner:
                    for entry, relative in tar_entries(inner, cancel, prefix="dev_flash"):
                        if entry.isdir():
                            continue
                        name = relative.as_posix().casefold()
                        if name in seen or len(seen) >= MAX_FILES:
                            raise ValueError("Colliding paths or too many firmware files.")
                        seen.add(name)
                        total += entry.size
                        if total > MAX_FIRMWARE:
                            raise ValueError("Firmware exceeds its expanded size limit.")
                        if shutil.disk_usage(home).free < entry.size + CHUNK:
                            raise ValueError("Not enough disk space for firmware extraction.")
                        with inner.extractfile(entry) as stream:
                            content = stream.read(entry.size + 1)
                        if len(content) != entry.size:
                            raise ValueError("Truncated firmware archive file.")
                        atomic_bytes(staging / relative, content, home)
            firmware_validator(staging / "dev_flash")
            cancel()
            destination = home / "dev_flash"
            if destination.exists():
                # A process can exit after publishing the directory but before
                # its stage is saved. Accept that narrow resume case only if
                # every file still matches a fresh extraction of this PUP.
                existing = set()
                for file in destination.rglob("*"):
                    reject_links_below(home, file)
                    if file.is_file():
                        relative = file.relative_to(home)
                        existing.add(relative.as_posix().casefold())
                        fresh = staging / relative
                        if not fresh.is_file() or file_digest(file) != file_digest(fresh):
                            raise ValueError("Prepared firmware changed. Start a new preparation session.")
                if existing != seen:
                    raise ValueError("Prepared firmware is incomplete. Start a new preparation session.")
                log("Verified previously published firmware against a fresh extraction.")
                return
            os.replace(staging / "dev_flash", destination)
            staging.rmdir()
            log("Firmware extracted and required audio module/fonts verified.")
    except tarfile.TarError:
        raise ValueError("Invalid or truncated firmware TAR archive.") from None


def pkg_module():
    # Reuse the SDK's established retail/debug package keystream, but provide
    # strict range/path validation and chunked writes here instead of its CLI.
    path = Path(__file__).resolve().parents[1] / "ps3recomp/tools/pkg_extract.py"
    spec = importlib.util.spec_from_file_location("buzz_pkg_extract", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_update(source, destination, title, log, cancel):
    from . import model
    from .setup_pipeline import reject_links_below
    source, destination = Path(source), Path(destination)
    reject_links_below(destination, destination)
    codec = pkg_module()
    with source.open("rb") as stream:
        size = source.stat().st_size
        header = stream.read(0xc0)
        if len(header) < 0xc0 or header[:4] != b"\x7fPKG":
            raise ValueError("Invalid PS3 update PKG.")
        revision, platform = unpack(">HH", header, 4)
        count = unpack(">I", header, 0x14)[0]
        declared, offset, length = unpack(">3Q", header, 0x18)
        if revision not in (0, 0x8000) or platform != 1:
            raise ValueError("Only PS3 retail/debug update packages are supported.")
        if declared != size or not 0 < count <= MAX_FILES or offset < 0xc0 or length > size - offset or count * 32 > length:
            raise ValueError("Invalid update PKG sizes/table.")
        mode = "debug" if revision == 0 else "finalized"
        def read(at, amount):
            cancel()
            if at < 0 or amount < 0 or amount > MAX_BINARY or at + amount > length:
                raise ValueError("Invalid update PKG data range.")
            block, skip = at // 16, at % 16
            blocks = (skip + amount + 15) // 16
            stream.seek(offset + block * 16)
            raw = stream.read(min(blocks * 16, length - block * 16))
            key = codec._keystream(mode, header[0x70:0x80], header[0x60:0xa0], block, blocks)
            plain = bytes(a ^ b for a, b in zip(raw, key))
            if len(plain) < skip + amount:
                raise ValueError("Truncated update PKG data.")
            return plain[skip:skip + amount]
        table = read(0, count * 32)
        plan, seen, total, sfo = [], set(), 0, None
        for i in range(count):
            name_at, name_size, data_at, data_size, flags, _ = unpack(">IIQQII", table, i * 32)
            if not 0 < name_size <= 4096 or name_at < len(table) or data_at + data_size > length:
                raise ValueError("Invalid update PKG entry range.")
            if flags & 0x10000000 or flags & 0xff not in (1, 2, 3, 4, 9):
                raise ValueError("Unsupported update PKG entry type.")
            try:
                relative = archive_path(read(name_at, name_size).decode("utf-8"))
            except UnicodeError:
                raise ValueError("Invalid update PKG filename.") from None
            key = relative.as_posix().casefold()
            if key in seen:
                raise ValueError("Colliding paths in update PKG.")
            seen.add(key)
            if flags & 0xff == 4:
                if data_size:
                    raise ValueError("Update PKG directory has data.")
                continue
            total += data_size
            if total > MAX_FIRMWARE:
                raise ValueError("Update PKG exceeds its extraction limit.")
            plan.append((relative, data_at, data_size))
            if relative.as_posix() == "PARAM.SFO":
                if data_size > CHUNK:
                    raise ValueError("Oversized update PARAM.SFO.")
                sfo = read(data_at, data_size)
        if sfo is None or model.title_id(sfo) != title or not any(path.as_posix() == "USRDIR/EBOOT.BIN" for path, _, _ in plan):
            raise ValueError("Update PKG title does not match the selected disc, or has no EBOOT.BIN.")
        destination.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(destination).free < total + CHUNK:
            raise ValueError("Not enough disk space for the update PKG.")
        for index, (relative, at, amount) in enumerate(plan):
            cancel()
            target = destination / relative
            reject_links_below(destination, target)
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".import-partial-" + uuid.uuid4().hex)
            try:
                with temporary.open("xb") as output:
                    done = 0
                    while done < amount:
                        chunk = read(at + done, min(CHUNK, amount - done))
                        output.write(chunk)
                        done += len(chunk)
                os.replace(temporary, target)
            finally:
                temporary.unlink(missing_ok=True)
            log(f"Update: {index + 1}/{len(plan)} files")
        log("Update extracted; title ID matches the selected disc.")

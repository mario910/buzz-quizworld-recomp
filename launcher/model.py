"""Small helpers shared by the preparation pipeline: PARAM.SFO title IDs and
atomic text writes."""
from __future__ import annotations

import os
from pathlib import Path
import struct
import tempfile


def atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def title_id(data: bytes) -> str:
    if len(data) < 20 or data[:4] != b'\0PSF':
        raise ValueError('Invalid PARAM.SFO.')
    keys, values, count = struct.unpack_from('<III', data, 8)
    if count > 256 or 20 + count * 16 > len(data) or keys >= len(data) or values >= len(data):
        raise ValueError('Corrupt PARAM.SFO table.')
    for n in range(count):
        key, fmt, length, capacity, offset = struct.unpack_from('<HHIII', data, 20 + 16 * n)
        end = data.find(b'\0', keys + key)
        if end < 0 or not keys <= keys + key < values or end >= values or length > capacity or values + offset + length > len(data):
            raise ValueError('Invalid PARAM.SFO field.')
        if data[keys + key:end] == b'TITLE_ID':
            if fmt != 0x204 or length > 32:
                raise ValueError('Invalid TITLE_ID.')
            return data[values + offset:values + offset + length].rstrip(b'\0').decode('ascii')
    raise ValueError('PARAM.SFO does not contain TITLE_ID.')

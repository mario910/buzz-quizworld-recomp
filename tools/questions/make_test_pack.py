"""Milestone 1 test pack: copy selected rounds of the disc pack (PACK0100)
into a new pack number, so we can check that the title discovers and lists
a second pack. Questions, speech and media are unchanged copies.

  python -I make_test_pack.py <POLPACK0100.PAK> <out dir> [--pack 101] [--rounds q_people]

Writes <out>/PACK0101/POLPACK0101.PAK. Nothing is written into game folders.
"""
import argparse
import struct
import sys
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import buzzq  # noqa: E402
import clu  # noqa: E402

ALIGN = 16  # every data offset in the shipped PAKs is 16-byte aligned


def write_stored_zip(path, entries):
    """entries: [(name, bytes)]. Stored ZIP laid out like the game's PAKs:
    zero padding before each local header so file data is ALIGN-aligned."""
    central = bytearray()
    with open(path, "wb") as out:
        for name, data in entries:
            nb = name.encode("ascii")
            pos = out.tell()
            pad = (-(pos + 30 + len(nb))) % ALIGN
            out.write(b"\0" * pad)
            hdr_off = out.tell()
            crc = zlib.crc32(data) & 0xFFFFFFFF
            out.write(struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, crc, len(data), len(data), len(nb), 0) + nb)
            out.write(data)
            central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0, 0, 0, crc, len(data), len(data),
                                   len(nb), 0, 0, 0, 0, 0, hdr_off) + nb
        cd_off = out.tell()
        out.write(central)
        n = len(entries)
        out.write(struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, n, n, len(central), cd_off, 0))


def round_files(roundpackages):
    """[(name, index)] from the generated RoundPackages script."""
    f = clu.load(roundpackages)
    ks = []
    for ins in f.code:
        if ins & 63 == 1:  # LOADK
            ks.append(f.consts[ins >> 14])
    groups = [ks[i:i + 5] for i in range(0, len(ks), 5)]  # name, n, 0, name, name
    return [(g[0], int(g[1])) for g in groups]


def set_pack_id(qak, kaq, pack):
    q, k = bytearray(qak), bytearray(kaq)
    if struct.unpack_from("<H", q, 0x12)[0] != 100 or struct.unpack_from(">H", k, 0x12)[0] != 100:
        raise ValueError("unexpected pack id field in round header")
    struct.pack_into("<H", q, 0x12, pack)
    struct.pack_into(">H", k, 0x12, pack)
    return bytes(q), bytes(k)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("pak")
    ap.add_argument("out")
    ap.add_argument("--pack", type=int, default=101)
    ap.add_argument("--rounds", nargs="+", default=["q_people"])
    a = ap.parse_args(argv)

    z = zipfile.ZipFile(a.pak)
    names = z.namelist()
    lang = names[0].split("/")[0]
    src_dir, dst_dir = "PACK0100", f"PACK{a.pack:04d}"
    read = lambda n: z.read(f"{lang}/{n}")  # noqa: E731
    entries = []

    # Scripts: register + shared category/difficulty/reveal tables unchanged.
    for n in ("SCRIPTS/register.clu", "SCRIPTS/GENERATED/questioncategories.clu",
              "SCRIPTS/GENERATED/questiondifficulty.clu", "SCRIPTS/GENERATED/questionreveals.clu"):
        entries.append((f"{lang}/{n}", read(n)))

    info = clu.load(read("SCRIPTS/GENERATED/addpackinfo.clu"))
    # AddPackInfo(1000, packId, order, ...): packId/order 100 -> new id; dir name.
    if clu.replace_consts(info, {100.0: float(a.pack), src_dir: dst_dir}) != 2:
        raise ValueError("addpackinfo constants not as expected")
    entries.append((f"{lang}/SCRIPTS/GENERATED/addpackinfo.clu", clu.dump(info)))

    available = dict(round_files(read("SCRIPTS/GENERATED/roundpackages.clu")))
    calls = []
    for r in a.rounds:
        calls.append((("PackageData", "NewPackage"), [r]))
        calls.append((("PackageData", "AddRoundFile"), [available[r], 0, r, r]))
    entries.append((f"{lang}/SCRIPTS/GENERATED/roundpackages.clu", clu.dump(clu.call_script(calls))))

    for n in names:
        if "/NAMEDTEXT/" in n or "/DATA/" in n:
            entries.append((n, z.read(n)))

    assets = set()
    tmp = Path(a.out) / "_rounds"
    tmp.mkdir(parents=True, exist_ok=True)
    for r in a.rounds:
        files = {ext: read(f"ROUNDS/{r}.{ext}") for ext in ("hdr", "hst", "qak", "kaq")}
        files["qak"], files["kaq"] = set_pack_id(files["qak"], files["kaq"], a.pack)
        for ext, data in files.items():
            entries.append((f"{lang}/ROUNDS/{r}.{ext}", data))
            (tmp / f"{r}.{ext}").write_bytes(data)
        rd = buzzq.parse_round(str(tmp / r))
        for q in rd["questions"]:
            for t in q.get("texts", []):
                if t.get("speech_file"):
                    assets.add(t["speech_file"])
            for m in q.get("media", []):
                if m.get("file"):
                    assets.add(m["file"])
                if m["type"] == "picture":
                    assets.update(n.split("/", 1)[1] for n in names if "/VIDEO/pics" in n)

    missing = sorted(x for x in assets if f"{lang}/{x}" not in names)
    if missing:
        raise SystemExit(f"referenced assets not in source pack: {missing[:5]} ...")
    for x in sorted(assets):
        entries.append((f"{lang}/{x}", read(x)))

    out = Path(a.out) / dst_dir
    out.mkdir(parents=True, exist_ok=True)
    pak = out / f"{lang}{dst_dir}.PAK"
    write_stored_zip(pak, entries)
    for t in tmp.iterdir():
        t.unlink()
    tmp.rmdir()

    check = zipfile.ZipFile(pak)
    assert check.testzip() is None
    print(f"{pak}: {len(entries)} entries, {pak.stat().st_size / 1e6:.1f} MB, "
          f"rounds={a.rounds}, assets={len(assets)}")


if __name__ == "__main__":
    main(sys.argv[1:])

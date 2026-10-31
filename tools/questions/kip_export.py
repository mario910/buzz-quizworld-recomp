"""Export Knowledge Is Power (PS4, CUSA09557) multiple-choice questions to a
neutral JSON pack source, for conversion into Buzz! Quiz World packs.

Input is a folder extracted with ps4pkg.py, containing at least:
  Media/resources.assets                         (question tables, TextAssets)
  Media/StreamingAssets/Localisation/*_text.loc  (UTF-16 "String ID<TAB>Text")
  Media/StreamingAssets/QuestionImages/QuestionImages.bytes   (ZIP of JPGs)
  Media/StreamingAssets/Audio/GeneratedSoundBanks/PS4/<Lang>/<lang>_<ID>.wem
    (voice; only listed here, extract with ps4pkg.py --filter)

  python -I kip_export.py <extract dir> <out.json> [--langs en pl ru]

Answer1 is always the correct answer (the game shuffles).
Only the 4-answer types are exported: TRV (global trivia), LST (per-language
local trivia) and FNL (finale). Sorting/linking/number questions have no Buzz
equivalent and are only counted.
"""
import argparse
import json
import re
import struct
import sys
import zipfile
from collections import Counter
from pathlib import Path

VOICE_DIRS = {"en": "English(UK)", "pl": "Polish", "ru": "Russian"}
# Trivia table columns (21, tab separated)
C_ID, C_KEY, C_ANS, C_CAT, C_IMAGE, C_FRAME, C_TAGS, C_DIFF, C_LINK = 0, 1, 2, 6, 15, 16, 17, 18, 20


AT9_GUID = bytes.fromhex("d242e147ba368d4d88fc61654f8c836c")   # 47E142D2-36BA-4D8D-88FC-61654F8C836C


def wem_to_at9(d):
    """Rewrap a Wwise ATRAC9 .wem (fmt tag 0xFFFC) as a standard AT9 WAV that
    FFmpeg decodes (WAVE_FORMAT_EXTENSIBLE + ATRAC9 GUID, version 1 + config)."""
    p, ch = 12, {}
    while p < len(d) - 8:
        cid, n = d[p:p + 4], struct.unpack_from("<I", d, p + 4)[0]
        ch[cid] = d[p + 8:p + 8 + n]
        p += 8 + n + (n & 1)
    f = ch[b"fmt "]
    tag, chn, rate, bps, align = struct.unpack_from("<HHIIH", f, 0)
    if tag != 0xFFFC:
        raise ValueError(f"not Wwise ATRAC9 (fmt tag {tag:#x})")
    spb, mask = struct.unpack_from("<HI", f, 18)
    config, (nsamp,) = f[24:28], struct.unpack_from("<I", f, 28)
    fmt = struct.pack("<HHIIHHH", 0xFFFE, chn, rate, bps, align, 0, 34) + struct.pack("<HI", spb, mask) \
        + AT9_GUID + struct.pack("<I", 1) + config + bytes(4)
    fact = struct.pack("<III", nsamp, 0, 0)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" + struct.pack("<I", len(fact)) + fact \
        + b"data" + struct.pack("<I", len(ch[b"data"])) + ch[b"data"]
    return b"RIFF" + struct.pack("<I", len(body)) + body


def text_assets(assets):
    """Name -> bytes for TextAssets whose names start with KIP_ (Unity
    serialized: u32 len + name, align 4, u32 len + data)."""
    d = Path(assets).read_bytes()
    out = {}
    for m in re.finditer(rb"([\x05-\x60])\x00\x00\x00(KIP_[A-Za-z]+_(?:QuestionText|KillList)[A-Za-z_\-]*)", d):
        name = m.group(2)
        if len(name) != m.group(1)[0]:
            continue
        p = (m.end(2) + 3) & ~3
        n = struct.unpack_from("<I", d, p)[0]
        if 0 < n < 20_000_000:
            out[name.decode()] = d[p + 4:p + 4 + n].decode("utf-8")
    return out


def rows(text):
    return [r.split("\t") for r in text.replace("\r", "").split("\n") if r.strip()]


def load_loc(path):
    t = Path(path).read_bytes().decode("utf-16")
    out = {}
    for line in t.split("\r\n")[1:]:
        if line:
            k, _, v = line.partition("\t")
            out[k] = v
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("out")
    ap.add_argument("--langs", nargs="+", default=["en", "pl", "ru"])
    a = ap.parse_args(argv)
    root = Path(a.root) / "Media"
    sa = root / "StreamingAssets"

    tables = text_assets(root / "resources.assets")
    loc = {l: load_loc(sa / "Localisation" / f"{l}_text.loc") for l in a.langs}
    kill = {l: set(rows(tables.get(f"KIP_Trivia_KillList_{l}", ""))[0]) if tables.get(f"KIP_Trivia_KillList_{l}") else set()
            for l in a.langs}
    images = {n.rsplit(".", 1)[0].lower(): n
              for n in zipfile.ZipFile(sa / "QuestionImages" / "QuestionImages.bytes").namelist()}
    voice = {}
    for l, d in VOICE_DIRS.items():
        p = sa / "Audio" / "GeneratedSoundBanks" / "PS4" / d
        voice[l] = p

    questions = []

    def add(r, kind, only_lang=None):
        key = r[C_KEY]  # text/voice key; LST keys repeat per language
        q = {
            "id": r[C_ID], "kind": kind,
            "category": [c for c in r[C_CAT:C_CAT + 9] if c] if len(r) > C_CAT else [],
            "difficulty": (r[C_DIFF] if len(r) > C_DIFF else "") or "NORMAL",
            "tags": r[C_TAGS].split("|~|") if len(r) > C_TAGS and r[C_TAGS] else [],
            "image": images.get(r[C_IMAGE].lower()) if len(r) > C_IMAGE and r[C_IMAGE] else None,
            "image_frame": (r[C_FRAME] if len(r) > C_FRAME else "") or None,
            "langs": {},
        }
        for l in a.langs:
            if only_lang and l != only_lang:
                continue
            if key in kill[l]:
                continue
            text = loc[l].get(key)
            answers = [loc[l].get(k) for k in r[C_ANS:C_ANS + 4]]
            if not text or not all(answers):
                continue
            q["langs"][l] = {"question": text, "answers": answers, "correct": 0,
                             "voice": f"{VOICE_DIRS[l]}/{l}_{key}.wem" if l in VOICE_DIRS else None}
        if q["langs"]:
            questions.append(q)

    for r in rows(tables["KIP_Trivia_QuestionText"]):
        add(r, "trivia")
    for l in a.langs:
        for r in rows(tables.get(f"KIP_Trivia_QuestionText_{l}", "")):
            add(r, "local", only_lang=l)
    for r in rows(tables["KIP_Finale_QuestionText"]):
        add(r, "finale")

    skipped = Counter()
    for name in tables:
        m = re.match(r"KIP_(Sorting|Linking)_QuestionText", name)
        if m and name.count("_") == 2:
            skipped[m.group(1)] += len(rows(tables[name]))

    Path(a.out).write_text(json.dumps({"source": "Knowledge Is Power (CUSA09557)", "questions": questions},
                                      ensure_ascii=False, indent=1), encoding="utf-8")
    by = Counter((q["kind"], l) for q in questions for l in q["langs"])
    print(f"{len(questions)} questions -> {a.out}")
    for (k, l), n in sorted(by.items()):
        print(f"  {k:7s} {l}: {n}")
    print("  with image:", sum(1 for q in questions if q["image"]))
    print("  difficulty:", dict(Counter(q["difficulty"] for q in questions)))
    print("  top categories:", dict(Counter(q["category"][0] for q in questions if q["category"])))
    print("  not exported (no Buzz equivalent):", dict(skipped))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main(sys.argv[1:])

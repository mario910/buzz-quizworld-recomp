"""Write Buzz! Quiz World round files (.hdr/.hst/.qak/.kaq).

A round is described by RoundModel (strings + media table + question rows);
write_round() serialises it. model_from_round() rebuilds a model from a parsed
shipped round, which lets `selftest` prove the writer byte-exact:

  python -I buzzround.py selftest <ROUNDS dir>

Layout and field meanings: docs/question-format.md.
"""
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import buzzq  # noqa: E402

COPYRIGHT = "(C) Copyright Relentless Software ltd., all rights reserved. "
QUESTION_FMT = "I4sIHHHHHHHHBBBBBBBB"   # 36 bytes, see section 1
MEDIA_FMT = "IIHHBBBB"                   # 16 bytes, section 0
NO_ROW = 0xFFFF


@dataclass
class Question:
    id: int
    difficulty: int                       # 1 easy, 2 medium, 3 hard
    texts: list                           # [(speech_no, string_no, kind)] kind 1 question, 2 topic
    answers: list                         # [(string_no, flags)] flags 5 correct, 1 wrong
    categories: list                      # [root id, sub id]
    round_no: int
    media: list = field(default_factory=list)   # rows in RoundModel.media
    text_no: int = 0
    reveal: int = 1
    unk20: int = 0
    unk22: int = 0x7FFE
    flags26: int = None                   # default: 8 without media, 0 with media
    b28: int = 0


@dataclass
class RoundModel:
    name: str
    pack_id: int
    lang: str
    strings: list                         # 1-based string numbers index into this
    questions: list
    media: list = field(default_factory=list)   # (local, asset, 0, pic_slot, type, 0x10, in_pics, 0)
    rounds_in_pack: int = 11              # header u16 at 0x10 (QW 11, Quiz TV 1)
    version: int = 1003


def _index(entries, off, e):
    return buzzq.build_index(entries, off, e)


def _qak(m: RoundModel, e):
    p = lambda fmt, *v: struct.pack(e + fmt, *v)  # noqa: E731
    lang = m.lang.encode("latin-1")[:3].ljust(4, b"\0")
    rows = {i: [] for i in range(7)}
    rows[0] = [p(MEDIA_FMT, *r) for r in m.media]
    for q in m.questions:
        i_round, i_media, i_speech = len(rows[6]), len(rows[2]), len(rows[3])
        i_cat, i_ans = len(rows[5]), len(rows[4])
        rows[6].append(p("H", q.round_no))
        for r in q.media:
            rows[2].append(p("HBB", r, 1, 0))
        for sp, sn, kind in q.texts:
            rows[3].append(p("IHBB", sp, sn, kind, 0))
        for c in q.categories:
            rows[5].append(p("H", c))
        for sn, fl in q.answers:
            rows[4].append(p("HBB", sn, fl, 0))
        flags26 = q.flags26 if q.flags26 is not None else (0 if q.media else 8)
        rows[1].append(struct.pack(e + "I", q.id) + lang + p(
            "IHHHHHHHHBBBBBBBB"[0:], q.text_no, i_round, i_media if q.media else NO_ROW, i_speech, i_cat,
            q.unk20, q.unk22, i_ans, flags26, q.b28, q.reveal, q.difficulty,
            1, len(q.media), len(q.texts), len(q.categories), len(q.answers)))

    head = bytearray(0x17C + 11 * 8)
    struct.pack_into(e + "HHHHIIHH", head, 0, 0xFFFE, 1, m.version, 0, 11, 0x17C, m.rounds_in_pack, m.pack_id)
    head[0x14:0x14 + len(m.name)] = m.name.encode("latin-1")
    head[0xD4:0xD4 + len(COPYRIGHT)] = COPYRIGHT.encode("latin-1")
    head[0x174:0x178] = lang
    head[0x178:0x17C] = b"\x01\0\0\0"

    out = bytearray(head)
    table = {}
    for i in range(7):
        data = b"".join(rows[i])
        table[i] = (len(out), len(rows[i])) if rows[i] else (0, 0)
        out += data
    out += bytes(-len(out) % 4)

    def counted(values):
        c = {}
        for v in values:
            c[v] = c.get(v, 0) + 1
        return list(c.items())
    idx = {
        8: [(q.id, 0) for q in m.questions],
        7: [(r[0], 0) for r in m.media],
        9: counted(c for q in m.questions for c in q.categories),
        10: counted(q.round_no for q in m.questions),
    }
    for i in (8, 7, 9, 10):
        table[i] = (len(out), 1)
        out += _index(idx[i], len(out), e)
    for i in range(11):
        off, cnt = table[i]
        struct.pack_into(e + "IH", out, 0x17C + 8 * i, off, cnt)
        out[0x17C + 8 * i + 6] = i
    return bytes(out)


def write_round(m: RoundModel):
    """Return {'hdr','hst','qak','kaq'} bytes."""
    hdr, hst = buzzq.encode_strings(m.strings)
    return {"hdr": hdr, "hst": hst, "qak": _qak(m, "<"), "kaq": _qak(m, ">")}


def model_from_round(base):
    r = buzzq.parse_round(str(base))
    h = r["header"]
    q = open(str(base) + ".qak", "rb").read()
    H = buzzq.parse_header(q)
    media = buzzq.sec_rows(q, H["sections"], 0) if H["sections"][0][1] else []
    row_of = {m[0]: i for i, m in enumerate(media)}
    qs = []
    for x in r["questions"]:
        raw = x["raw"]
        qs.append(Question(
            id=x["id"], difficulty=x["difficulty"],
            texts=[(t["speech_id"], t["string_no"], t["kind"]) for t in x["texts"]],
            answers=[(a["string_no"], a["flags"]) for a in x["answers"]],
            categories=x["categories"], round_no=x["round_ids"][0],
            media=[row_of[mm["local_id"]] for mm in x["media"]],
            text_no=x["text_no"], reveal=x["reveal"], unk20=raw["unk20"], unk22=raw["unk22"],
            flags26=raw["flags26"], b28=raw["b28"]))
    return RoundModel(name=h["name"], pack_id=h["pack_id"], lang=h["lang"], strings=r["_strings"],
                      questions=qs, media=[tuple(m) for m in media], rounds_in_pack=h["unk10"],
                      version=h["version"])


def _selftest(d):
    bad = 0
    for hdr in sorted(Path(d).glob("*.hdr")):
        base = hdr.with_suffix("")
        out = write_round(model_from_round(base))
        diffs = [ext for ext in out if out[ext] != Path(f"{base}.{ext}").read_bytes()]
        print(f"{base.name:22s} {'OK' if not diffs else 'DIFF ' + ','.join(diffs)}")
        bad += bool(diffs)
    return bad


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "selftest":
        sys.exit(1 if _selftest(sys.argv[2]) else 0)
    print(__doc__)

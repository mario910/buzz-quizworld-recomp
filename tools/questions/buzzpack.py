"""Build a Buzz! Quiz World question pack (<LANG>PACKnnnn.PAK) from a JSON
source, or straight from a Knowledge Is Power export (kip_export.py).

  python -I buzzpack.py build <source.json> <template POLPACK0100.PAK> <out dir> --pack 102
  python -I buzzpack.py from-kip <kip_questions.json> <source.json> --lang pl [--limit N]

Source JSON:
  {"pack": {"title": "...", "topic": "default topic line"},
   "questions": [{"id": "...", "subject": 175, "category": 224, "difficulty": 1..3,
                  "question": "...", "topic": "...", "answers": ["correct", "w1", "w2", "w3"],
                  "image": "path.jpg" | null, "speech": "path.rsf" | null}]}
subject/category are Quiz World category ids (root/sub, see questioncategories.clu
in the template); they are re-registered under this pack's own ids and internal
names, with the same CAT_* text keys. pack.category_base can choose a free global
category range in 1..999 (defaults: pack 101=500, 102=600, ... 105=900).

What the builder writes (all validated by re-parsing):
  SCRIPTS/register.clu, GENERATED/{questioncategories,addpackinfo,roundpackages}.clu
  ROUNDS/p<pack>_<subject>.{hdr,hst,qak,kaq}  one round per used subject
  QUESTIONASSETS/VIDEO/pics001.sdi           picture sheet (frame 0 placeholder)
  QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/*.rsf   speech: .rsf copied; wav/mp3/KiP .wem
                                                   encoded to ATRAC3 (at3enc.py, needs
                                                   numpy); else one shared silent clip
  NAMEDTEXT/*, DATA/*                        copied from the template pack
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
import zipfile
import zlib
from collections import OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import buzzq  # noqa: E402
import buzzround  # noqa: E402
import clu  # noqa: E402
import rsf  # noqa: E402
import sdi  # noqa: E402
from make_test_pack import write_stored_zip  # noqa: E402

LANG_CODES = {"pl": "POL", "ru": "RUS", "cs": "CZE", "el": "GRC", "hr": "CRO"}
MAX_PICTURES = 427            # SDI index area 0x50..0x800 holds 428 entries, frame 0 is the placeholder
MAX_CATEGORY_ID = 999         # AddQuestionCategoryItem silently rejects ids outside 1..999


def category_name(pack_no, name):
    """Internal lookup name; CAT_* remains the localised display key."""
    return f"p{pack_no}_{name}"


def template_categories(pak, lang):
    """[(id, name, key, parent, nMedia, nText, packId, roundNo)] from the template."""
    f = clu.load(pak.read(f"{lang}/SCRIPTS/GENERATED/questioncategories.clu"))
    items, cur = [], None
    for ins in f.code:
        op, bx = ins & 63, ins >> 14
        if op == 5 and f.consts[bx] == "AddQuestionCategoryItem":
            cur = []
        elif op == 1 and cur is not None:
            cur.append(f.consts[bx])
        elif op == 28 and cur is not None:
            if len(cur) == 8:
                items.append((int(cur[0]), cur[1], cur[2], int(cur[3]), *map(int, cur[4:])))
            cur = None
    return items


def placeholder_image(path):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "color=c=0x202060:s=640x368",
                    "-frames:v", "1", str(path)], check=True)


def build(src, template, out_dir, pack_no):
    s = json.loads(Path(src).read_text(encoding="utf-8"))
    tz = zipfile.ZipFile(template)
    lang = tz.namelist()[0].split("/")[0]
    cats = {c[0]: c for c in template_categories(tz, lang)}
    dir_name = f"PACK{pack_no:04d}"
    entries = OrderedDict()
    topic_default = s.get("pack", {}).get("topic", "")

    qs = s["questions"]
    for q in qs:
        if q["subject"] not in cats or cats[q["subject"]][3] != 0:
            raise ValueError(f"{q['id']}: subject {q['subject']} is not a root category")
        if cats.get(q["category"], (0, 0, 0, None))[3] != q["subject"]:
            raise ValueError(f"{q['id']}: category {q['category']} is not under {q['subject']}")
        if len(q["answers"]) != 4:
            raise ValueError(f"{q['id']}: needs 4 answers")

    # --- categories live in a GLOBAL 1..999 registry, not a pack-local u16
    # namespace. Reserve 100-id blocks above the disc's highest id (436)
    # for packs 101..105. Other pack numbers can choose category_base.
    # Internal names must also be unique: AddRoundNew resolves them globally.
    # Round numbers are this pack's round-file indices 1..k: AddRoundFile(n),
    # the question records' round list and the categories all use them (the
    # disc pack uses 1..11, Quiz TV packs 1..36; larger values hang the round).
    used_roots = sorted({q["subject"] for q in qs}, key=lambda r: cats[r][7])
    category_base = s.get("pack", {}).get("category_base", 500 + (pack_no - 101) * 100)
    if type(category_base) is not int:
        raise ValueError("category_base must be an integer")
    new_id, round_of = {}, {}
    n = 1
    for k, r in enumerate(used_roots, 1):
        new_id[r] = category_base + n - 1
        n += 1
        round_of[r] = k
        for c in sorted({q["category"] for q in qs if q["subject"] == r}):
            new_id[c] = category_base + n - 1
            n += 1
    if n > 100:
        raise ValueError("too many categories for one pack (99)")
    if not new_id or not all(1 <= c <= MAX_CATEGORY_ID for c in new_id.values()):
        raise ValueError("category ids must be in 1..999; set pack.category_base to a free range")
    if set(new_id.values()) & cats.keys():
        raise ValueError("category ids overlap the template; choose a free pack.category_base")

    # --- pictures: one sheet, frame 0 placeholder, slot = frame index
    images = []
    for q in qs:
        if q.get("image") and q["image"] not in images:
            images.append(q["image"])
    if len(images) > MAX_PICTURES:
        raise ValueError(f"{len(images)} distinct pictures; one sheet holds {MAX_PICTURES}")
    slot = {img: i + 1 for i, img in enumerate(images)}
    if images:
        with tempfile.TemporaryDirectory() as td:
            ph = Path(td) / "placeholder.png"
            placeholder_image(ph)
            sheet = sdi.build_picture_sheet(_template_sheet(tz, lang, td), [ph] + images, name="pics001")
        entries[f"{lang}/QUESTIONASSETS/VIDEO/pics001.sdi"] = sheet

    # --- speech: .rsf copied, other audio encoded to ATRAC3; else one shared silent clip
    speech_no, silent_no = {}, pack_no * 100000
    gain = float(s.get("pack", {}).get("speech_gain_db", 0.0))
    jobs = {}
    for i, q in enumerate(qs, 1):
        if q.get("speech"):
            speech_no[q["id"]] = pack_no * 100000 + i
            jobs[f"{lang}/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/{6000000000 + speech_no[q['id']]}.rsf"] = q["speech"]
    entries.update(encode_speech(jobs, gain))
    if any(q["id"] not in speech_no for q in qs):
        entries[f"{lang}/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/{6000000000 + silent_no}.rsf"] = rsf.silence(1.0)

    # --- one round per subject
    counts = {}
    files = {}
    package_calls = []
    for r in used_roots:
        name = f"p{pack_no}_{re.sub('[^a-z]', '', cats[r][1].lower())}"[:24]
        files[r] = name
        strings, rqs, media = [], [], []
        group = [q for q in qs if q["subject"] == r]
        group.sort(key=lambda q: 0 if q.get("image") else 1)   # media questions first, as shipped
        for k, q in enumerate(group, 1):
            base = len(strings)
            strings += [q["question"], q.get("topic") or topic_default] + q["answers"]
            m = []
            if q.get("image"):
                local = slot[q["image"]]
                if not any(row[0] == local for row in media):
                    # Raw media +0xE is an aspect-ratio enum, not a sheet flag:
                    # 1=4:3, 2=2:3, 3=1:1, 4=16:9 (GetQuestionResourceAspectRatio).
                    media.append((local, local, 0, local, 3, 0x10, 4, 0))
                m = [next(i for i, row in enumerate(media) if row[0] == local)]
            sp = speech_no.get(q["id"], silent_no)
            rqs.append(buzzround.Question(
                id=pack_no * 100000 + len(counts.get("ids", [])) + k,
                difficulty=int(q.get("difficulty", 2)),
                texts=[(sp, base + 1, 1), (sp, base + 2, 2)],
                answers=[(base + 3, 5), (base + 4, 1), (base + 5, 1), (base + 6, 1)],
                categories=[new_id[r], new_id[q["category"]]], round_no=round_of[r],
                media=m, reveal=1 + (k % 6)))
        counts.setdefault("ids", []).extend(q.id for q in rqs)
        order = sorted(range(len(media)), key=lambda i: media[i][0])
        remap = {old: new for new, old in enumerate(order)}
        media = [media[i] for i in order]
        for q in rqs:
            q.media = [remap[i] for i in q.media]
        model = buzzround.RoundModel(name=name, pack_id=pack_no, lang=lang, strings=strings,
                                     questions=rqs, media=media, rounds_in_pack=len(used_roots))
        for ext, data in buzzround.write_round(model).items():
            entries[f"{lang}/ROUNDS/{name}.{ext}"] = data
        package_calls += [(("PackageData", "NewPackage"), [name]),
                          (("PackageData", "AddRoundFile"), [round_of[r], 0, name, name])]
        for q in group:
            c = counts.setdefault(q["category"], [0, 0])
            c[0 if q.get("image") else 1] += 1
            c = counts.setdefault(r, [0, 0])
            c[0 if q.get("image") else 1] += 1

    # --- scripts
    cat_calls = []
    for r in used_roots:
        for c in [r] + sorted(x for x in new_id if x != r and cats[x][3] == r):
            _, nm, key, parent, *_ = cats[c]
            cat_calls.append((("AddQuestionCategoryItem",),
                              [new_id[c], category_name(pack_no, nm), key, new_id[parent] if parent else 0,
                               counts[c][0], counts[c][1], pack_no, round_of[r]]))
    gen = f"{lang}/SCRIPTS/GENERATED"
    entries[f"{gen}/questioncategories.clu"] = clu.dump(clu.call_script(cat_calls))
    # A pack game needs its own games and rounds (Quiz TV DLC model, still
    # supported by Quiz World): RoundSetup runs before the package list.
    games = {m: f"p{pack_no}PackGame{m}" for m in ("1p", "4p", "8p")}
    # The native chooser descends into subcategories. Each selectable category
    # needs an AddRoundNew entry (0x10D668 maps the chosen id to a round).
    # Registering just roots leaves no choices and falls back to the first round.
    round_categories = [(cats[c][1], files[cats[c][3] or c]) for c in new_id]
    entries[f"{gen}/roundsetup.clu"] = clu.dump(clu.call_script(
        round_setup_calls(pack_no, dir_name, games, round_categories)))
    entries[f"{gen}/roundpackages.clu"] = clu.dump(clu.call_script(
        [(("IncludeScript",), ["Generated/RoundSetup"])] + package_calls))
    title_key, desc_key = f"PackTitlePk{pack_no}Pack", f"PackDescPk{pack_no}Pack"
    info = clu.load(tz.read(f"{gen}/addpackinfo.clu"))
    if clu.replace_consts(info, {100.0: float(pack_no), "PACK0100": dir_name,
                                 "PackTitlePk100Pack": title_key, "PackDescPk100Pack": desc_key,
                                 "p100PackGame1p": games["1p"], "p100PackGame4p": games["4p"],
                                 "p100PackGame8p": games["8p"]}) != 7:
        raise ValueError("unexpected AddPackInfo template")
    entries[f"{gen}/addpackinfo.clu"] = clu.dump(info)
    # register: categories first (no ClearAll, unlike the disc pack), then pack info
    entries[f"{lang}/SCRIPTS/register.clu"] = clu.dump(clu.call_script(
        [(("IncludeScript",), ["Generated/QuestionCategories"]),
         (("IncludeScript",), ["Generated/AddPackInfo"])]))
    # NAMEDTEXT: the template's strings (CAT_* keys etc.) plus this pack's title,
    # description and game names.
    pack = s.get("pack", {})
    title = pack.get("title", dir_name)
    extra = {title_key: title, desc_key: pack.get("description", title)}
    extra.update({g: title for g in games.values()})
    base = f"{lang}/NAMEDTEXT/default"
    nt = named_text(tz.read(base + ".hdr"), tz.read(base + ".hst"), tz.read(base + ".ndx"), extra)
    for ext, data in nt.items():
        entries[f"{base}.{ext}"] = data
    for n_ in tz.namelist():
        if "/DATA/" in n_:
            entries[n_] = tz.read(n_)

    out = Path(out_dir) / dir_name
    out.mkdir(parents=True, exist_ok=True)
    # DLC packs on the HDD are found as <LANG>PACKnnnn.edat (a plain ZIP is
    # fine: the runtime serves non-NPD .edat files as plaintext).
    pak = out / f"{lang}{dir_name}.edat"
    write_stored_zip(pak, list(entries.items()))
    _validate(pak, lang)
    return pak, dict(questions=len(qs), rounds=len(used_roots), pictures=len(images),
                     categories=len(new_id), size_mb=round(pak.stat().st_size / 1e6, 1))


# Quiz World's own game model (Global/Scripts/GameSetup, RoundSetupDebug):
# AddGame(name, id, kind, maxPlayers, 0, -1, 1, 0, 4) and
# AddRoundNew(id, game, channel group, round type, category name, pack dir, round file).
# Plans and all nine AddGame arguments copy the Knowledge channel games.
# Omitting the penultimate 0 shifts 4 into a boolean flag and leaves the
# native choice-count field at zero. Quiz TV's AddRound differs from AddRoundNew.
GAME_PLANS = {
    "1p": [("TriggerFinger", 0)] * 3,
    "4p": [("PointBuilder", 0), ("PassTheBomb", 0), ("FastestFingerFirst", 0), ("PieFight", 0),
           ("PointStealerMix", 0), ("BetIt", 0), ("FinalCountdown", 8)],
    "8p": [("PointBuilder", 0), ("TriggerFinger", 0), ("BetIt", 0), ("FinalCountdown", 8)],
}
GAME_KIND = {"1p": ("1PPACKSPECIFIC", "Max1Player"), "4p": ("4PPACKSPECIFIC", "Max4Players"),
             "8p": ("8PPACKSPECIFIC", "Max8Players")}
# Channel group per Quiz World root category name
CHANNEL = {"Music": "Music", "Movies": "TVCinema", "TV": "TVCinema", "Sport": "Sport",
           "Leisure": "Lifestyle"}


def round_setup_calls(pack_no, dir_name, games, subjects):
    """AddGame / AddGamePlanRounds / AddRoundNew calls for this pack's games.
    subjects: [(Quiz World category/subcategory name, containing round file)]."""
    calls = []
    for i, (m, g) in enumerate(games.items()):
        kind, maxp = GAME_KIND[m]
        calls.append((("AddGame",), [g, pack_no * 1000 + i, kind, maxp, 0, -1, 1, 0, 4]))
    for m, g in games.items():
        for rt, n in GAME_PLANS[m]:
            calls.append((("AddGamePlanRounds",), [g, rt, n]))
    rid = pack_no * 1000 + 300
    for cat_name, f in subjects:
        for m, g in games.items():
            for rt in dict.fromkeys(rt for rt, _ in GAME_PLANS[m]):
                calls.append((("AddRoundNew",), [rid, g, CHANNEL.get(cat_name, "Knowledge"), rt,
                                                 category_name(pack_no, cat_name), dir_name, f]))
                rid += 1
    return calls


def ndx_hash(key):
    """NAMEDTEXT key hash (same CRC as the SDI header), signed int32."""
    v = (zlib.crc32(key.encode()) ^ 0xFFFFFFFF ^ 0xFABCDEF7) & 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def named_text(hdr, hst, ndx, extra):
    """Append {key: text} to a NAMEDTEXT table; returns {'hdr','hst','ndx'}."""
    strings = buzzq.decode_strings(hdr, hst)
    lines = [l for l in ndx.decode("utf-8-sig").splitlines() if l.strip()]
    have = {int(l.split()[0]) for l in lines}
    for key, text in extra.items():
        h = ndx_hash(key)
        if h in have:
            raise ValueError(f"NAMEDTEXT key {key} already exists")
        strings.append(text)
        lines.append(f"{h} {len(strings)}")
    h_, s_ = buzzq.encode_strings(strings)
    crlf = chr(13) + chr(10)
    ndx_text = chr(0xFEFF) + crlf.join(lines) + crlf
    return {"hdr": h_, "hst": s_, "ndx": ndx_text.encode("utf-8")}


def _speech_job(args):
    path, gain = args
    if str(path).lower().endswith(".rsf"):
        return Path(path).read_bytes()
    import at3enc                      # needs numpy
    return at3enc.encode_file(path, gain)


def encode_speech(jobs, gain):
    """{entry name: source path} -> {entry name: .rsf bytes}; non-.rsf sources
    (wav/mp3/ogg/... or KiP .wem) are encoded to ATRAC3 in parallel."""
    import os
    from concurrent.futures import ProcessPoolExecutor
    if not jobs:
        return {}
    names = list(jobs)
    with ProcessPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) - 2)) as ex:
        data = list(ex.map(_speech_job, [(jobs[n], gain) for n in names], chunksize=16))
    return dict(zip(names, data))


def _template_sheet(tz, lang, td):
    p = Path(td) / "template.sdi"
    p.write_bytes(tz.read(f"{lang}/QUESTIONASSETS/VIDEO/pics002.sdi"))
    return p


def _validate(pak, lang):
    z = zipfile.ZipFile(pak)
    assert z.testzip() is None
    names = set(z.namelist())
    categories = template_categories(z, lang)
    category_ids = {c[0] for c in categories}
    if len(category_ids) != len(categories) or not all(1 <= c <= MAX_CATEGORY_ID for c in category_ids):
        raise AssertionError("duplicate category ids or ids outside the game's 1..999 registry")
    if len({c[1] for c in categories}) != len(categories):
        raise AssertionError("duplicate internal category names")
    if any(c[3] and c[3] not in category_ids for c in categories):
        raise AssertionError("unregistered parent category")
    with tempfile.TemporaryDirectory() as td:
        for n in names:
            if n.endswith(".qak"):
                base = Path(td) / Path(n).stem
                for ext in ("hdr", "hst", "qak", "kaq"):
                    Path(f"{base}.{ext}").write_bytes(z.read(n[:-3] + ext))
                r = buzzq.parse_round(str(base))
                if buzzq.swap_qak(Path(f"{base}.qak").read_bytes()) != Path(f"{base}.kaq").read_bytes():
                    raise AssertionError(f"{n}: kaq is not the swapped qak")
                for q in r["questions"]:
                    if not q["question"] or len(q["answers"]) != 4 or not q["answers"][0]["correct"]:
                        raise AssertionError(f"{n}: bad question {q['id']}")
                    if not q["categories"] or not set(q["categories"]) <= category_ids:
                        raise AssertionError(f"{n}: unregistered category for question {q['id']}")
                    for t in q["texts"]:
                        if f"{lang}/{t['speech_file']}" not in names:
                            raise AssertionError(f"{n}: missing {t['speech_file']}")
    for n in names:
        if n.endswith(".sdi"):
            sdi.check(z.read(n))
        if n.endswith(".clu"):
            clu.load(z.read(n))


# ---------------------------------------------------------------- KiP adapter
# KiP category path (level 2, level 3) -> Quiz World (root, sub) category ids
KIP_MAP = {
    ("Activities", "Games"): (188, 196), ("Activities", "Sport"): (174, 247),
    ("Belief", None): (250, 251), ("Creative", "Art"): (177, 178),
    ("Creative", "Music"): (175, 224), ("Creative", "Writing"): (177, 179),
    ("History", None): (182, 184), ("Lifestyle", "Home"): (188, 189),
    ("Lifestyle", "Interests"): (188, 191), ("Lifestyle", "Self"): (235, 238),
    ("ModernWorld", "MWEvents"): (182, 183), ("ModernWorld", "MWHumanity"): (230, 232),
    ("ModernWorld", "MWInternet"): (235, 236), ("ModernWorld", "MWTheWorld"): (250, 253),
    ("Performance", "Movies"): (199, 208), ("Performance", "Stage"): (177, 181),
    ("Performance", "TV"): (257, 271), ("Science", "GeneralScience"): (235, 237),
    ("Science", "LifeScience"): (235, 240), ("Science", "PhysicalScience"): (235, 239),
}
KIP_DIFFICULTY = {"EASY": 1, "NORMAL": 2, "EXPERT": 3}


def from_kip(kip, lang, limit=None, images_dir=None, include_finale=False, voice_dir=None):
    j = json.loads(Path(kip).read_text(encoding="utf-8"))
    out = []
    for q in j["questions"]:
        t = q["langs"].get(lang)
        if not t or (q["kind"] == "finale" and not include_finale):
            continue
        c = q["category"]
        root_sub = KIP_MAP.get((c[1], c[2]) if len(c) > 2 else None) or (KIP_MAP.get((c[1], None)) if len(c) > 1 else None)
        if not root_sub:
            root_sub = (182, 187)   # Events / Trends for uncategorised (finale)
        a = t["answers"]
        out.append(dict(id=q["id"], subject=root_sub[0], category=root_sub[1],
                        difficulty=KIP_DIFFICULTY.get(q["difficulty"], 2), question=t["question"],
                        topic=None, answers=[a[t["correct"]]] + [x for i, x in enumerate(a) if i != t["correct"]],
                        image=str(Path(images_dir) / q["image"]) if images_dir and q["image"] else None,
                        speech=_voice(voice_dir, t.get("voice"))))
    if limit:
        out = out[:limit]
    # KiP voice is ~4.5 dB quieter than Buzz's own question speech
    return {"pack": {"title": "WIEDZA TO POTĘGA", "description": "Pytania z gry Wiedza to potęga (PS4)",
                     "topic": "Wiedza to potęga", "speech_gain_db": 4.5},
            "questions": out}


def _voice(voice_dir, rel):
    if not voice_dir or not rel:
        return None
    p = Path(voice_dir) / rel
    return str(p) if p.exists() else None


def main(argv):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("source")
    b.add_argument("template")
    b.add_argument("out")
    b.add_argument("--pack", type=int, default=102)
    k = sub.add_parser("from-kip")
    k.add_argument("kip")
    k.add_argument("out")
    k.add_argument("--lang", default="pl")
    k.add_argument("--limit", type=int)
    k.add_argument("--images", help="folder with the extracted KiP JPEGs")
    k.add_argument("--include-finale", action="store_true")
    k.add_argument("--voice", help="GeneratedSoundBanks/PS4 folder with <Lang>/<lang>_<ID>.wem")
    a = ap.parse_args(argv)
    if a.cmd == "build":
        pak, stats = build(a.source, a.template, a.out, a.pack)
        print(pak, stats)
    else:
        s = from_kip(a.kip, a.lang, a.limit, a.images, a.include_finale, a.voice)
        Path(a.out).write_text(json.dumps(s, ensure_ascii=False, indent=1), encoding="utf-8")
        print(a.out, len(s["questions"]), "questions,", sum(1 for q in s["questions"] if q["speech"]), "with voice")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main(sys.argv[1:])

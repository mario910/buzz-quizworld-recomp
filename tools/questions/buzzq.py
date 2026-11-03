"""Buzz! Quiz World (PS3, BCES00647) question-round / named-text decoder.

Usage (always run with `python -I`):
  python -I buzzq.py strings  <base>            # base = path without extension (.hdr/.hst)
  python -I buzzq.py round    <base>            # parse <base>.qak + .hdr/.hst, print JSON
  python -I buzzq.py checkkaq <base>            # verify <base>.kaq == byte-swapped <base>.qak
  python -I buzzq.py dumpall  <ROUNDS dir> <out.json> [categories.json]
  python -I buzzq.py roundtrip <base>           # re-encode strings, compare to .hdr/.hst bytes
"""
import struct, sys, os, json, math, collections
from pathlib import Path

# ----------------------------------------------------------------------------
# .hdr / .hst  : static Huffman-coded UTF-16 string table
# ----------------------------------------------------------------------------
EOS = 0xFEFE          # end-of-string symbol (its freq == number of strings)
TABLE_END = 0xFE00    # 2-byte terminator after the last (sym,freq) row

def read_hdr(h):
    if h[0:2] != b'\x00\xff':
        raise ValueError('bad hdr magic %r' % h[:2])
    nstr, total = struct.unpack_from('>II', h, 2)
    rows = []
    o = 10
    while True:
        sym = struct.unpack_from('<H', h, o)[0]
        if sym == TABLE_END and o + 2 == len(h):
            break
        freq = struct.unpack_from('>I', h, o + 2)[0]
        rows.append((sym, freq))
        o += 6
    return nstr, total, rows

def build_tree(rows):
    """Exact tree the game uses (verified: decodes every byte of all 12 files).
    rows: [(sym,freq)] in file order (ascending freq, ties by ascending sym).
    Work list = rows in that order. Repeatedly pop the first two items (a,b);
    node = (child0=a, child1=b), weight a+b; insert node AFTER every existing
    item whose weight <= node weight (i.e. after equal-weight items)."""
    L = [(f, sym) for sym, f in rows]         # leaf = int symbol, node = tuple
    while len(L) > 1:
        a = L.pop(0); b = L.pop(0)
        w = a[0] + b[0]
        k = 0
        while k < len(L) and L[k][0] <= w:
            k += 1
        L.insert(k, (w, (a[1], b[1])))
    return L[0][1]

def decode_strings(h, s):
    nstr, total, rows = read_hdr(h)
    tree = build_tree(rows)
    out = []; pos = 0; nsym = 0
    for _ in range(nstr):
        t = tree; cur = []
        while True:
            bit = (s[pos >> 3] >> (7 - (pos & 7))) & 1   # MSB-first
            pos += 1
            t = t[bit]
            if isinstance(t, int):
                nsym += 1
                if t == EOS:
                    break
                cur.append(chr(t)); t = tree
        out.append(''.join(cur))
        pos = (pos + 7) & ~7            # every string starts byte-aligned
    if pos // 8 != len(s) or nsym != total:
        raise ValueError('hst decode mismatch: used %d/%d bytes, %d/%d symbols'
                         % (pos // 8, len(s), nsym, total))
    return out

def load_strings(base):
    return decode_strings(Path(base + '.hdr').read_bytes(), Path(base + '.hst').read_bytes())

# --- encoder (for writing new files) ----------------------------------------
def encode_strings(strings):
    """Returns (hdr_bytes, hst_bytes). Frequencies include EOS once per string."""
    cnt = collections.Counter()
    for st in strings:
        for ch in st:
            c = ord(ch)
            if c > 0xFFFF or c in (EOS, TABLE_END):
                raise ValueError('unencodable char %r' % ch)
            cnt[c] += 1
    cnt[EOS] += len(strings)
    rows = sorted(cnt.items(), key=lambda kv: (kv[1], kv[0]))   # (freq, sym)
    total = sum(cnt.values())
    hdr = bytearray(b'\x00\xff' + struct.pack('>II', len(strings), total))
    for sym, f in rows:
        hdr += struct.pack('<H', sym) + struct.pack('>I', f)
    hdr += struct.pack('<H', TABLE_END)
    tree = build_tree(rows)
    codes = {}
    def walk(t, prefix):
        if isinstance(t, int):
            codes[t] = prefix or '0'
        else:
            walk(t[0], prefix + '0'); walk(t[1], prefix + '1')
    walk(tree, '')
    hst = bytearray()
    for st in strings:
        bits = ''.join(codes[ord(c)] for c in st) + codes[EOS]
        bits += '0' * (-len(bits) % 8)
        hst += int(bits, 2).to_bytes(len(bits) // 8, 'big') if bits else b''
    return bytes(hdr), bytes(hst)

# ----------------------------------------------------------------------------
# .qak (little endian) / .kaq (big endian) question database
# ----------------------------------------------------------------------------
# record layouts per section index (struct format w/o endian prefix)
SEC_FMT = {
    0: 'IIHHBBBB',            # media: localId, assetNo, u16 0, picSlot, type, 0x10, aspect, 0
    1: 'I4sIHHHHhHHHBBBBBBBB',  # question (36 bytes), see FORMAT.md
    2: 'HBB',                 # question->media link: sec0 row, u8 1, u8 0
    3: 'IHBB',                # speech/text link: speechNo, stringNo(1-based), kind, 0
    4: 'HBB',                 # answer: stringNo(1-based), flags(4=correct), 0
    5: 'H',                   # category id
    6: 'H',                   # round/category-file index (categories.clu last field)
}
INDEX_SECTIONS = (7, 8, 9, 10)  # sorted-key indexes over sec0.localId, sec1.id, sec5, sec6
MEDIA_TYPES = {1: 'music', 2: 'video', 3: 'picture'}
SPEECH_BASE = 6000000000
ASSET_BASE = 1000000

def parse_header(q, e='<'):
    magic, one, ver, ver_hi, nsec, tbl, f10, packid = struct.unpack_from(e + 'HHHHIIHH', q, 0)
    hdr = dict(magic=magic, unk02=one, version=ver, unk06=ver_hi, nsections=nsec,
               section_table=tbl, unk10=f10, pack_id=packid,
               name=q[0x14:0xd4].rstrip(b'\0').decode('latin1'),
               copyright=q[0xd4:0x174].rstrip(b'\0').decode('latin1'),
               lang=q[0x174:0x178].rstrip(b'\0').decode('latin1'),
               unk178=list(q[0x178:0x17c]))
    secs = {}
    for i in range(nsec):
        off, cnt, idx, _pad = struct.unpack_from(e + 'IHBB', q, tbl + 8 * i)
        secs[idx] = (off, cnt)
    hdr['sections'] = secs
    return hdr

def sec_rows(q, secs, idx, e='<'):
    off, cnt = secs[idx]
    fmt = e + SEC_FMT[idx]; sz = struct.calcsize(fmt)
    return [struct.unpack_from(fmt, q, off + sz * i) for i in range(cnt)]

def parse_index(q, off, e='<'):
    mn, mx, nb, bp, ne, ep = struct.unpack_from(e + '6I', q, off)
    entries = [struct.unpack_from(e + 'II', q, ep + 8 * i) for i in range(ne)]
    buckets = [struct.unpack_from(e + 'II', q, bp + 8 * i) for i in range(nb)]
    return dict(min=mn, max=mx, entries=entries, buckets=buckets,
                entries_off=ep - off, buckets_off=bp - off)

def parse_round(base, e='<', ext='.qak'):
    q = Path(base + ext).read_bytes()
    strs = load_strings(base)
    H = parse_header(q, e)
    secs = H['sections']
    R = {i: (sec_rows(q, secs, i, e) if secs.get(i, (0, 0))[1] else []) for i in SEC_FMT}
    S = lambda n: strs[n - 1] if 0 < n <= len(strs) else None   # string numbers are 1-based
    media = []
    for r in R[0]:
        local, asset, z0, slot, typ, b10, aspect, b0 = r
        m = dict(local_id=local, asset_no=asset, type=MEDIA_TYPES.get(typ, typ),
                 pic_slot=slot, in_pics=aspect, aspect_ratio_code=aspect,
                 aspect_ratio={1: '4:3', 2: '2:3', 3: '1:1', 4: '16:9'}.get(aspect),
                 raw=[z0, b10, b0])  # in_pics retained for older callers
        if typ == 1: m['file'] = 'QUESTIONASSETS/MUSIC/STREAMED/%d.rsf' % (ASSET_BASE + asset)
        elif typ == 2: m['file'] = 'QUESTIONASSETS/VIDEO/%d.sdi' % (ASSET_BASE + asset)
        media.append(m)
    questions = []
    for qi, r in enumerate(R[1]):
        (qid, lang, textno, i_round, i_media, i_speech, i_cat, unk20, unk22,
         i_ans, flags26, b28, reveal, diff, n_round, n_media, n_speech, n_cat, n_ans) = r
        speech = [R[3][i_speech + k] for k in range(n_speech)]
        texts = [dict(string_no=sn, kind=kind, speech_id=sp, speech_file=
                      'QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/%d.rsf' % (SPEECH_BASE + sp),
                      text=S(sn)) for sp, sn, kind, _ in speech]
        answers = [dict(string_no=sn, flags=fl, correct=bool(fl & 4), text=S(sn))
                   for sn, fl, _ in (R[4][i_ans + k] for k in range(n_ans))]
        mlist = []
        if i_media != 0xFFFF:
            for k in range(n_media):
                row, one, z = R[2][i_media + k]
                mlist.append(media[row])
        qtype = 'text' if not mlist else '+'.join(m['type'] for m in mlist)
        questions.append(dict(
            index=qi, id=qid, lang=lang.rstrip(b'\0').decode('latin1'), text_no=textno,
            question=next((t['text'] for t in texts if t['kind'] == 1), None),
            topic=next((t['text'] for t in texts if t['kind'] == 2), None),
            answers=answers,
            correct=[a['text'] for a in answers if a['correct']],
            categories=[R[5][i_cat + k][0] for k in range(n_cat)],
            round_ids=[R[6][i_round + k][0] for k in range(n_round)],
            difficulty=diff, reveal=reveal, type=qtype, media=mlist, texts=texts,
            raw=dict(i_round=i_round, i_media=i_media, i_speech=i_speech, i_cat=i_cat,
                     i_ans=i_ans, unk20=unk20, unk22=unk22, flags26=flags26, b28=b28,
                     counts=[n_round, n_media, n_speech, n_cat, n_ans])))
    idx = {i: parse_index(q, secs[i][0], e) for i in INDEX_SECTIONS if secs.get(i, (0, 0))[1]}
    return dict(header={k: v for k, v in H.items() if k != 'sections'},
                sections={i: [hex(o), c] for i, (o, c) in sorted(secs.items())},
                nstrings=len(strs), questions=questions, media=media,
                indexes={i: dict(min=v['min'], max=v['max'], n=len(v['entries']),
                                 nbuckets=len(v['buckets'])) for i, v in idx.items()},
                _strings=strs)

# ---- .qak -> .kaq byte swap (used to verify which fields are swapped) -------
def swap_qak(q):
    """Produce the big-endian twin of a .qak by re-packing every known field."""
    out = bytearray(q)
    H = parse_header(q, '<')
    def rep(fmt, off):
        v = struct.unpack_from('<' + fmt, q, off); struct.pack_into('>' + fmt, out, off, *v)
    rep('HHHHIIHH', 0)
    for i in range(H['nsections']):
        rep('IHBB', H['section_table'] + 8 * i)
    for i, (off, cnt) in H['sections'].items():
        if not cnt: continue
        if i in SEC_FMT:
            sz = struct.calcsize('<' + SEC_FMT[i])
            for k in range(cnt): rep(SEC_FMT[i], off + sz * k)
        else:
            ix = parse_index(q, off, '<')
            rep('6I', off)
            for k in range(len(ix['entries'])): rep('II', off + ix['entries_off'] + 8 * k)
            for k in range(len(ix['buckets'])): rep('II', off + ix['buckets_off'] + 8 * k)
    return bytes(out)

def build_index(keys_values, sec_off, e='<'):
    """Serialise an index section placed at absolute file offset sec_off.
    keys_values: [(key, value)] (value = occurrence count for sec9/sec10, 0 for sec7/sec8).
    Layout (matches all shipped files): 24-byte header, entries, then buckets."""
    E = sorted(keys_values)
    n = len(E)
    if n == 0:
        return bytes(24)          # empty index: all-zero header, pointers 0
    stride = math.isqrt(n) if n else 0
    nb = n // stride if stride else 0
    B = [(E[i * stride][0], i * stride) for i in range(nb)]
    ep = sec_off + 24; bp = ep + 8 * n
    out = struct.pack(e + '6I', E[0][0] if E else 0, E[-1][0] if E else 0, nb, bp, n, ep)
    out += b''.join(struct.pack(e + 'II', *kv) for kv in E)
    out += b''.join(struct.pack(e + 'II', *kv) for kv in B)
    return out

def main(argv):
    if hasattr(sys.stdout, 'reconfigure'): sys.stdout.reconfigure(encoding='utf-8')
    cmd = argv[1]
    if cmd == 'strings':
        for i, s in enumerate(load_strings(argv[2]), 1): print(i, s)
    elif cmd == 'round':
        r = parse_round(argv[2]); r.pop('_strings')
        print(json.dumps(r, ensure_ascii=False, indent=1))
    elif cmd == 'checkkaq':
        q = open(argv[2] + '.qak', 'rb').read(); k = open(argv[2] + '.kaq', 'rb').read()
        sw = swap_qak(q)
        diff = [i for i in range(len(k)) if sw[i] != k[i]]
        print(os.path.basename(argv[2]), 'len', len(q), len(k), 'mismatching bytes:', len(diff),
              [hex(x) for x in diff[:20]])
    elif cmd == 'roundtrip':
        strs = load_strings(argv[2])
        h, s = encode_strings(strs)
        print(os.path.basename(argv[2]), 'hdr equal', h == open(argv[2] + '.hdr', 'rb').read(),
              'hst equal', s == open(argv[2] + '.hst', 'rb').read())
    elif cmd == 'dumpall':
        import glob
        cats = json.load(open(argv[4], encoding='utf-8')) if len(argv) > 4 else {}
        out = {}
        for qak in sorted(glob.glob(os.path.join(argv[2], '*.qak'))):
            base = qak[:-4]
            r = parse_round(base); r.pop('_strings')
            for qq in r['questions']:
                qq['category_names'] = [cats.get(str(c), {}).get('name') for c in qq['categories']]
            out[os.path.basename(base).lower()] = r
        json.dump(out, open(argv[3], 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
        print('wrote', argv[3])
    else:
        print(__doc__)

if __name__ == '__main__':
    main(sys.argv)

# Buzz! Quiz World (BCES00647) question/text file formats

Status legend: **[C]** confirmed (byte-exact re-encode, or holds for every record in all 11 POL rounds); **[G]** guessed / semantics unknown.
Reference implementation: `buzzq.py` in this directory (decode, encode strings, .qak->.kaq swap, index rebuild).

## 1. `.hdr` + `.hst` — Huffman string table (rounds and NAMEDTEXT use the same codec)

### .hdr (big-endian header, mixed-endian rows)
| off | type | meaning |
|---|---|---|
| 0x00 | u8[2] `00 FF` | magic [C] |
| 0x02 | u32 BE | number of strings N [C] |
| 0x06 | u32 BE | total symbols, which is the sum of all freqs, including one EOS per string [C] |
| 0x0A | rows | 6-byte rows `{u16 LE symbol, u32 BE freq}` [C] |
| end-2 | u16 LE `0xFE00` | end of table, with no freq [C] (the game's loader `func_004D9C00` reads u16s until 0xFE00) |

The earlier attempt failed because it read the rows as `[freq][sym]` from offset 12. That pairs each freq with the next row's symbol.
- Rows are sorted by **(freq asc, symbol asc)** [C: encoder reproduces every .hdr byte-exact].
- Symbol `0xFEFE` = end-of-string (EOS). Its freq = N [C].
- Symbols are UTF-16 code units (Polish diacritics are used directly; no surrogates seen).

### Tree construction (exact; the encoder reproduces all 12 .hst files byte-for-byte) [C]
```
L = rows in file order, each item (weight=freq, leaf symbol)
while len(L) > 1:
    a = L.pop(0); b = L.pop(0)              # two lowest (front of list)
    n = node(child0=a, child1=b, weight=a.w+b.w)
    insert n into L immediately AFTER the last item with weight <= n.weight
         (new node goes behind existing equal-weight items)
root = L[0]
```
Bit 0 selects child0 (the first popped), and bit 1 selects child1.

### .hst bitstream [C]
- Strings are stored one after another in string-number order. Each string's codes are followed by the EOS code.
- Bits are read **MSB-first** within each byte.
- **Each string starts on a byte boundary.** After EOS, the remaining bits of that byte are zero padding. The game's decoder `func_004D9110` (ppu_recomp_026.cpp) works byte by byte, bit 7 down to 0, with node+4 = child0, node+8 = child1, node+1 = leaf flag and node+0x14 = symbol, and stops on 0xFEFE.
- There is no per-string offset table, so the game finds string *k* by scanning. The file ends exactly after the last string's padding byte.
- String numbers used by .qak/.ndx are **1-based**.

Encoding = count chars (+N×EOS), sort rows (freq,sym), write hdr, build tree as above, emit codes, pad each string to a byte. `buzzq.encode_strings()` does this and round-trips all shipped files exactly.

## 2. `NAMEDTEXT/default.ndx`
UTF-8 text with a BOM. Lines end in CRLF: `<signed int32 hash> <string number>`, one line per string. The lines are in string order 1..N (323 in POL) [C].
The hash function / key names are **not identified** [G]. CRC32 (both bit orders), FNV-1/1a, djb2, sdbm and ×31/×65599 hashes of the CAT_* keys, the English category names and the pack keys did not match. The loader references `.ndx` via TOC slot r2-0x3BC (ppu_recomp_017.cpp:22024, around 0x2FFAFC). The key hash should be found in the lookup path from there.

## 3. `.qak` (little-endian) / `.kaq` (big-endian)
`.kaq` is the field-wise byte-swapped twin of `.qak` [C: `buzzq.py checkkaq` gives 0 differing bytes on all 11 files]. The game presumably reads `.kaq`. These fields are **not swapped** because they are byte fields: header 0x174..0x17B, the u8 index/pad of each section-table entry, the trailing u8s of records, and all string data.

### Header
| off | type | value / meaning |
|---|---|---|
| 0x000 | u16 | 0xFFFE (bytes `FE FF` in .qak, `FF FE` in .kaq) magic [C] |
| 0x002 | u16 | 1 [G: version/minor] |
| 0x004 | u16 | 1003 [G: format version] |
| 0x006 | u16 | 0 |
| 0x008 | u32 | 11 = number of section-table entries [C] |
| 0x00C | u32 | 0x17C = section table offset [C] |
| 0x010 | u16 | 11 [G: number of rounds in pack?] |
| 0x012 | u16 | 100 = pack id (PACK0100) [C-ish] |
| 0x014 | char[0xC0] | round name `q_people`, zero padded |
| 0x0D4 | char[0xA0] | `(C) Copyright Relentless Software ltd., all rights reserved. ` zero padded |
| 0x174 | char[4] | language `POL\0` |
| 0x178 | u8[4] | `01 00 00 00` [G: language count?] (not swapped) |
| 0x17C | 11 × 8 | section table: `u32 offset, u16 count, u8 sectionIndex, u8 0` [C] |

An empty section has offset 0 and count 0 (miniquiz sections 0 and 2). Index sections (7-10) always have count 1.
Observed file layout: header, table (ends 0x1D4), sec0, sec1, sec2, sec3, sec4, sec5, sec6, [pad to 4], sec8, sec7, sec9, sec10. EOF is at the end of sec10.

### Section 0: media (16 B/row), sorted by localId
`u32 localId, u32 assetNo, u16 0, u16 picSlot, u8 type, u8 0x10, u8 inPics, u8 0`
- type 1 = music: `QUESTIONASSETS/MUSIC/STREAMED/{1000000+assetNo}.rsf` [C: every one exists]
- type 2 = video: `QUESTIONASSETS/VIDEO/{1000000+assetNo}.sdi` [C: every one exists]
- type 3 = picture: picSlot 1..414 is unique per picture across the pack. [G] It is the frame in `QUESTIONASSETS/VIDEO/pics001.sdi`, whose header says 0x1A4 = 420 frames. inPics = 1 for 492 pictures and 0 for 13 [G: meaning unknown]. assetNo is not a file in the pack.
- localId is consistent across rounds (same id gives the same row). Rows are shared by several questions.
- 0x10 and the u16 0 are constants [G].

### Section 1: questions (36 B/row), not sorted
| off | type | meaning |
|---|---|---|
| 0 | u32 | question id (unique, ~0x58xxx-0x5Bxxx) [C] |
| 4 | char[4] | `POL\0` |
| 8 | u32 | textNo [G: source-DB text id; not a string index] |
| 12 | u16 | first row in sec6 (round id) |
| 14 | u16 | first row in sec2 (media link), 0xFFFF = none |
| 16 | u16 | first row in sec3 (texts/speech) |
| 18 | u16 | first row in sec5 (categories) |
| 20 | u16 | 0 [G] |
| 22 | u16 | 0x7FFE [G: always] |
| 24 | u16 | first row in sec4 (answers) |
| 26 | u16 | 8 if no media, 0 if media [G: flag] |
| 28 | u8 | 0 |
| 29 | u8 | 1..6 [G: picture reveal effect, see questionreveals.clu: 1 Pixellate, 2 Blocks, 3 Sliding Blocks, 4 Zoom Out, 5 Swirly, 6 Distort, 7 Fade; set on text questions too] |
| 30 | u8 | difficulty 1 Easy / 2 Medium / 3 Hard (questiondifficulty.clu) [C-ish] |
| 31 | u8 | count in sec6 (always 1) |
| 32 | u8 | count in sec2 (0/1 = media count) |
| 33 | u8 | count in sec3 (2; 1 in miniquiz) |
| 34 | u8 | count in sec5 (2) |
| 35 | u8 | count in sec4 (4) |
Rows are laid out contiguously: question k uses rows k·count in each child section. All media questions come first.

### Section 2: question to media (4 B): `u16 sec0Row, u8 1, u8 0`
### Section 3: texts + speech (8 B): `u32 speechNo, u16 stringNo (1-based), u8 kind, u8 0`
- kind 1 = question text, kind 2 = topic/heading line (e.g. "Czas na transport"). Miniquiz has no kind 2.
- Both rows of a question share speechNo, so there is one speech file per question: `QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/{6000000000+speechNo}.rsf` [C: all 6173 exist; 6173 questions].
### Section 4: answers (4 B): `u16 stringNo (1-based), u8 flags, u8 0`
- flags 5 = correct (bit 2), 1 = wrong. The correct answer is **always listed first** in all 6173 questions, so the game must shuffle [C for data; bit meaning G].
### Section 5: categories (2 B): `u16 categoryId`
- Two per question: root (e.g. 230 People) and subcategory (231..234). The ids, English names and parents come from `SCRIPTS/GENERATED/questioncategories.clu`: `AddQuestionCategoryItem(id, name, key, parentId, nMediaQs, nTextQs, packId, roundNo)`. The root's nMediaQs/nTextQs match these files (People 66/156).
### Section 6: round number (2 B): `u16` = the categories.clu roundNo (1 = miniquiz ... 11 = tv).
### String order in .hst
For each question in sec1 order: question, topic, then 4 answers (6 strings). Miniquiz has 5 strings: question and 4 answers. Section 3/4 hold explicit stringNos, so this order is only convention.

### Sections 7-10: sorted lookup indexes (all u32)
`u32 minKey, u32 maxKey, u32 nBuckets, u32 bucketsOff(abs), u32 nEntries, u32 entriesOff(abs)`, followed by entries `{u32 key, u32 value}` sorted by key, then buckets `{u32 key, u32 entryIndex}`.
- stride = floor(sqrt(nEntries)), nBuckets = floor(nEntries/stride), bucket i = (entries[i·stride].key, i·stride) [C: rebuilds byte-exact].
- An empty index is 24 zero bytes.
- sec7 = sec0.localId (value 0); sec8 = sec1.questionId (value 0); sec9 = distinct sec5 category ids (value = number of questions); sec10 = distinct sec6 round numbers (value = number of questions). [C]
- `buzzq.build_index(keys_values, abs_offset)` writes these.

## 4. Writing new rounds: checklist
1. Build the string list and the qak rows. Re-encode the strings with `encode_strings`. Set `.hdr` N to the string count.
2. Fill sec0..6 so the row ranges in sec1 are consistent. Pad to 4 bytes after sec6 (16-bit sections).
3. Rebuild sec7..10 with `build_index`, then fix the section table offsets.
4. Write the .qak little-endian and the .kaq by swapping only the multi-byte fields (`swap_qak`).
5. Unknowns that a writer should copy from an existing record: textNo, the 0x7FFE word, the flags26 word (8 = no media), reveal byte, and the sec0 constants.
6. New categories also require `questioncategories.clu` counts. Speech, music and video files are looked up by id.

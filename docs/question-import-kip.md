# Knowledge Is Power (PS4) question import

Source: `EP9000-CUSA09557_00-1710231057KPE3M0`, a fake-signed PKG (`drm_type 0xF`, 2.1 GB).
Status: unpacked and decoded. Questions export to JSON, pictures are plain
JPEGs and the voice decodes with FFmpeg. Conversion into Buzz packs is not written yet.

## Unpacking (`tools/questions/ps4pkg.py`)

Python port of LibOrbisPkg's documented fake-PKG algorithms. No third-party
binaries are run.

1. EKPFS = `SHA256(SHA256(be32 index=1) || SHA256(content_id padded to 48) || passcode)`.
   The passcode is all `'0'` x32 (the fake-PKG default) and works for this package.
2. Outer PFS (mode 0xD: signed, encrypted): XTS-AES-128, 0x1000-byte sectors
   from sector 16. Keys = `HMAC-SHA256(EKPFS, le32 1 || seed)`, tweak = first 16 bytes,
   data = last 16 bytes. PKG `pfs_flags` has no "new crypt" bit, so the plain EKPFS is used.
3. `uroot/pfs_image.dat` is a PFSC (zlib-block) image of the inner PFS. Inode
   flag 1 = compressed; `size` is stored bytes and `size_c` is the logical length.
4. Inner PFS: 17,714 files, a Unity (IL2CPP) game.

```
python -I tools/questions/ps4pkg.py list    <pkg>
python -I tools/questions/ps4pkg.py extract <pkg> <out> --filter "StreamingAssets/(Localisation|QuestionImages)|Media/resources\.assets$"
```

## Data layout

| Path under `Media/` | Contents |
| --- | --- |
| `resources.assets` | Unity TextAssets holding the question tables (TSV) |
| `StreamingAssets/Localisation/<lang>_text.loc` | UTF-16LE TSV `String ID<TAB>Text`, 24 languages |
| `StreamingAssets/QuestionImages/QuestionImages.bytes` | plain ZIP of 304 JPEGs |
| `StreamingAssets/Audio/GeneratedSoundBanks/PS4/<Lang>/<lang>_<ID>.wem` | one voice clip per question |

### Question tables (TextAssets in `resources.assets`)
- `KIP_Trivia_QuestionText`: global trivia (`TRV`), 3,813 rows, 21 tab columns:
  `0 id, 1 text key, 2-5 answer keys (Answer1 = correct), 6-14 category path
  (up to 5 used: e.g. Experience > ModernWorld > MWHumanity > HuPolitics > EUPolitics),
  15 image name, 16 image framing (Portrait/Landscape), 17 tags joined by "|~|",
  18 difficulty (EASY/NORMAL/EXPERT), 20 related category`.
- `KIP_Trivia_QuestionText_<lang>`: per-language local questions (`LST`). The id
  carries a language suffix (`LST00001pl`) and the text key is shared (`LST00001`), but
  the questions differ per language. There are 750 for Polish.
- `KIP_Finale_QuestionText`: 300 finale questions (`FNL`), 4 answers.
- `KIP_Trivia_KillList_<lang>`: ids hidden for a language.
- Sorting (`SRT`/`LSS`), linking (`LNK`/`LSL`) and number guess (`GSS`) types have
  no Buzz equivalent and are not exported.

### Voice
`.wem` = RIFF with Wwise format tag `0xFFFC` = **ATRAC9**, mono, 48 kHz, 224-byte
frames, config `FE 70 06 F0`. Rewrapping it as a standard AT9 WAV
(`WAVE_FORMAT_EXTENSIBLE`, GUID `47E142D2-36BA-4D8D-88FC-61654F8C836C`, version 1 +
config) lets FFmpeg decode it to PCM. Tested on `pl_TRV00002`: 6.6 s, real speech levels.

| Voice language | TRV | LST | FNL | other |
| --- | --- | --- | --- | --- |
| Polish | 3,862 | 750 | 300 | 175 |
| Russian | 3,862 | 750 | 300 | 175 |
| English (UK) | 5,254 | 757 | 376 | 323 |

Text exists in 24 languages; voice only in these three.

## Export (`tools/questions/kip_export.py`)

```
python -I tools/questions/kip_export.py <extract dir> kip_questions.json --langs en pl ru
```

| Kind | en | pl | ru |
| --- | --- | --- | --- |
| trivia (TRV) | 3,813 | 3,812 | 3,813 |
| local (LST) | 650 | 750 | 750 |
| finale (FNL) | 300 | 300 | 300 |

545 picture questions (every image name resolves to a JPEG). Difficulty: 2,550 EXPERT,
3,372 NORMAL, 341 EASY. Top categories: Imagination, Entertainment,
Awareness, Experience.

## Mapping onto Buzz

| KiP | Buzz! Quiz World |
| --- | --- |
| 4 answers, Answer1 correct | 4 answers, correct first (flag 5) |
| EASY / NORMAL / EXPERT | difficulty 1 / 2 / 3 |
| category path + tags | `AddQuestionCategoryItem` tree (new ids) |
| JPEG image | frame in a new `pics00N.sdi` (MPEG-2 still, 640x368) |
| ATRAC9 voice (pl/ru/en) | ATRAC3 `.rsf` speech, one per question. Decode with FFmpeg, re-encode ATRAC3 |
| per-language questions | one `<LANG>PACKnnnn.PAK` per language |

Remaining work: the Buzz pack builder (`buzzpack build`), the ATRAC3 encoder
choice, and the picture-sheet SDI writer. Pack titles need the NAMEDTEXT `.ndx` hash
or reuse of existing keys.

# Custom question packs

Custom packs register, appear in Pack Game and can display questions,
answers and pictures. The builder registers valid category IDs and
subcategory round records. Broader finale/player-count coverage and voice
quality still need manual validation. Byte-level formats are documented in
[question-format.md](question-format.md).

Tools: `tools/questions/buzzpack.py`, `buzzq.py`, `luadis.py` and `at3enc.py`.
Addresses below are specific to the executable named in the surrounding
reference; do not relocate EU 1.02 bindings into EU 1.00 by a global delta.

## Capabilities

| Goal | Feasible? | Notes |
| --- | --- | --- |
| Read every question (text, answers, category, difficulty, media) | **Yes, done** | 6,173 POL questions decode; re-encoding is byte-exact |
| Write new round files | **Yes** | Huffman text, `.qak`/`.kaq` and indexes rebuild byte-exact; custom questions display in game |
| New packs selectable by players | **Yes, the menu already exists** | Pack Game and Manage Quiz Packs appear when DLC packs are registered |
| Text, picture, video and music questions | **Yes** | Pictures are frames in `pics00N.sdi`, video is `.sdi`, music is `.rsf` |
| Question speech / per-language sets | **Yes, with an ATRAC3 encoder** | All audio is ATRAC3 at 48 kHz; one PAK per language |
| Import Buzz! Quiz TV packs | **Very likely as whole packs** | Same containers and codec; Quiz World's loader accepts the Quiz TV file version |
| Import Knowledge Is Power (PS4) | **Yes** | Unpacked and exported: 4,862 Polish questions with Polish voice, 545 pictures. See [question-import-kip.md](question-import-kip.md) |

## Where questions live

Every `.PAK` is a **stored (uncompressed) ZIP**. Each language has its own PAK:

```
USRDIR/PACK0100/POLPACK0100.PAK
  POL/SCRIPTS/register.clu                      IncludeScript() of the generated scripts
  POL/SCRIPTS/GENERATED/questioncategories.clu  category tree (295 items)
  POL/SCRIPTS/GENERATED/questiondifficulty.clu  Easy=1 Medium=2 Hard=3
  POL/SCRIPTS/GENERATED/questionreveals.clu     picture reveals 1..7
  POL/SCRIPTS/GENERATED/addpackinfo.clu         AddPackInfo(...) then RoundPackages
  POL/SCRIPTS/GENERATED/roundpackages.clu       PackageData.NewPackage / AddRoundFile
  POL/ROUNDS/q_<subject>.{hdr,hst,qak,kaq}      11 subject files
  POL/NAMEDTEXT/default.{hdr,hst,ndx}           pack UI strings
  POL/QUESTIONASSETS/SPEECH/QUESTIONS/ONDEMAND/{6000000000+n}.rsf   one per question
  POL/QUESTIONASSETS/MUSIC/STREAMED/{1000000+n}.rsf
  POL/QUESTIONASSETS/VIDEO/{1000000+n}.sdi, pics001..003.sdi
```

POL pack 100 contents:

| Round | Total | Text | Picture | Video | Music |
|---|---|---|---|---|---|
| artsandliterature | 211 | 145 | 62 | 3 | 1 |
| events | 259 | 167 | 78 | 14 | 0 |
| leisure | 464 | 310 | 152 | 2 | 0 |
| miniquiz | 1455 | 1455 | 0 | 0 | 0 |
| movies | 729 | 591 | 100 | 35 | 3 |
| music | 1078 | 590 | 210 | 35 | 243 |
| people | 222 | 156 | 46 | 16 | 4 |
| scienceandnature | 312 | 121 | 120 | 71 | 0 |
| sport | 541 | 284 | 221 | 35 | 1 |
| theworld | 305 | 158 | 136 | 11 | 0 |
| tv | 597 | 459 | 108 | 29 | 1 |
| **Total** | **6173** | **4436** | **1233** | **251** | **253** |

## Format summary (details in question-format.md)

- **Text** (`.hdr` + `.hst`): frequency-table Huffman over UTF-16 code units.
  `0xFEFE` ends a string, each string is byte-aligned, and string numbers are 1-based. The game decoder
  is `func_004D9110`. Re-encoding reproduces all shipped files exactly.
- **Questions** (`.qak` LE, `.kaq` = field-wise byte-swapped twin): 36-byte
  question records with id, difficulty, reveal and row links to media, text,
  answers (4; correct one listed first, flag 5) and category/subcategory/round.
  Also contains sorted lookup indexes (all rebuildable).
  Loader `func_000E4828` accepts versions 1002 (Quiz TV) and 1003 (Quiz World).
- **Lua** (`.clu`): Lua 5.1 bytecode, big-endian, float32 numbers. We can ship
  source compiled by a matching `luac` build, or generate bytecode ourselves.
- **Audio** (`.rsf`): 32-byte header + raw ATRAC3 frames, 48 kHz. Speech is mono
  with 0x98-byte frames, music is stereo. Decoding works with FFmpeg; **encoding needs an ATRAC3
  encoder** (e.g. open-source `atracdenc`) or a port-side PCM path.
- **Video / pictures** (`.sdi`): `SDIVIDEO` header + packet index + MPEG-2 PS,
  640x368. Picture questions point at a frame slot (1..414) in `pics00N.sdi`.
  New pictures mean encoding stills as MPEG-2 frames (FFmpeg) and writing the SDI index.
- **NAMEDTEXT hash:** `crc32(key) ^ 0xFFFFFFFF ^ 0xFABCDEF7`, interpreted as
  signed int32. The builder adds its own title, description and game-name keys.

## Pack selection already in the game

- Main menu > **Pack Game**: play one pack, or "Play All Packs".
- Extras > **Manage Quiz Packs**: enable or disable each pack, or delete DLC packs.
- Both appear when `PackManagement.HaveDownloadPacks()` is true. The C++ side
  exposes `GetNumberOfPacks`, `GetPackDetails`, `Enable/DisablePackAtIndex`.
- `BUZZ_HIDE_PSN_STORE` only removes the Store item, not these menus.

## Injection points in the port

1. **Filesystem**: the runtime maps `/dev_hdd0` and `/dev_bdvd` to host folders,
   so custom `PACKnnnn/<LANG>PACKnnnn.PAK` files can live outside the original
   disc data. **Next step**: trace which DLC directory and file names the title probes
   (`DLCHDD_PATH`, `%s/GAME/%s/%s`, `.edat`) by logging failed opens and opendir.
2. **Lua**: `port/frontend_policy.cpp` already runs port-owned Lua in the
   title's VM after a named include. The same mechanism can call `AddPackInfo` and
   `IncludeScript` for packs that the native scan doesn't find.
3. **UCQ / MyBuzz** user-created quiz XML (`LoadDownloadedQuizFileIntoGame`).
   This is a text-only fallback that needs a local stand-in for the old service. Not planned.

## Importing other games

### Buzz! Quiz TV (BCES00305, on disk)
- Packs `PACK0002..0006, 0018, 0023` (5,512 questions), plain ZIPs, same
  Huffman/qak/rsf/sdi formats, same `AddPackInfo` 13-arg signature.
- No ID collisions with pack 100 (question ids 7556-63258 vs 362053-373460).
- Differences to handle: Quiz TV registers rounds with the older 13-arg `AddRound` +
  `AddGame`/`AddGamePlanRounds` (Quiz World still registers these natives, arity
  unverified). Pack title, description and subject strings live in Quiz TV's base NAMEDTEXT,
  so they must be imported too. The category table is a single `UNCATEGORISED` node. Video
  runs at 25 fps (vs 29.97).
- Fallback: convert questions into Quiz World-style `q_<category>` files using the encoder.

### Knowledge Is Power (PS4, CUSA09557)
- Unpacked with `tools/questions/ps4pkg.py` (Python port of the fake-PKG
  algorithms, all-zero passcode). It is a Unity game. Questions are TSV TextAssets and
  `.loc` text (24 languages), pictures are a ZIP of JPEGs, and voice is Wwise ATRAC9
  (pl/ru/en), which FFmpeg decodes. Details: [question-import-kip.md](question-import-kip.md).
- `tools/questions/kip_export.py` exports 3,812 trivia + 750 Polish-only + 300
  finale questions in Polish, plus en/ru.

## Pack builder (`tools/questions/buzzpack.py`)

Builds `<LANG>PACKnnnn.edat` (a plain ZIP; see the in-game results below) from a JSON
source, or from a KiP export. Packs register, list and play in game.
Everything is checked by re-parsing the output.

```
python -I tools/questions/buzzpack.py from-kip kip_questions.json kip_pl.json --lang pl --images <KiP JPEG dir>
python -I tools/questions/buzzpack.py build kip_pl.json <USRDIR>/PACK0100/POLPACK0100.PAK <out> --pack 102
```

Pieces, each verified independently:
- `buzzround.py`: round writer. Rebuilding all 11 shipped rounds from their parsed form
  is byte-exact for `.qak`, `.kaq`, `.hdr` and `.hst` (`buzzround.py selftest`).
- `sdi.py`: picture sheets. FFmpeg encodes MPEG-1 intra pictures, and the tool writes the CRI
  Sofdec mux, header packs, index and loader checksum. The checksum formula is verified on all
  shipped SDIs, and the output decodes in FFmpeg.
- `rsf.py`: ATRAC3 `.rsf` info and decode to WAV via FFmpeg. It can also generate a
  digitally silent clip (−91 dB), used as placeholder speech.
- `clu.py`: generated scripts (categories, round packages, register) plus the patched
  `AddPackInfo`.

KiP Polish result (pack 102): 4,562 questions in 10 subject rounds, 304 pictures, 30 categories,
15.4 MB. Spot check: the Einstein question's slot 1 frame decodes to the Einstein picture.

Current builder registration:
- Category ids are global **1..999**, enforced by `AddQuestionCategoryItem`
  (`func_0013C6E8`, range check at 0x13C744). The template has 295 categories,
  with ids reaching **436**. An out-of-range registration returns without adding
  anything; having nonempty strings in the archive does not ensure a playable question.
- Default category blocks: pack 101 starts at 500, 102 at 600, through 105 at 900.
  Set `pack.category_base` in the source JSON for another free block. A pack uses
  consecutive ids from that base (up to 99); co-installed custom packs must have
  disjoint ranges. Invalid ranges and collisions with template ids fail before
  expensive media encoding.
- Internal category names are `p<pack>_<original name>` in both category registration
  and `AddRoundNew`. Name lookup is global (`func_000E28F0`), so retaining a disc
  category name can select the disc's id. Existing `CAT_*` keys still supply the
  localised display names. There is no `ClearAll`.
- Round-file indices are 1..k in `AddRoundFile`, question records and categories;
  file names are `p<pack>_<subject>`.
- Question ids are `pack*100000+n`, and the topic line is a fixed pack string.
- `AddPackInfo` points to this pack's own NAMEDTEXT keys and `p<pack>PackGame*` plans.
- `Generated/RoundPackages` includes `Generated/RoundSetup` before the package list.
  The latter registers the Quiz World games, plans and `AddRoundNew` entries.
- **v6:** `AddGame` needs nine arguments, matching the native binding and
  Knowledge games: `(name, id, kind, maxPlayers, 0, -1, 1, 0, 4)`.
  The earlier eight-argument call put 4 into a boolean flag and left the choice
  count at 0. The user reported repeating Arts and Literature / Point Builder
  across rounds. v6 corrected the call; v7 below fixed the remaining selection bug.
- **v7:** `AddRoundNew` must cover **every registered category, including
  subcategories**, pointing to its subject's question file. Native selection
  descends into the category tree; `func_0010D668` maps selected category ids
  to these entries. With only roots registered, that mapping returned no
  choices, and `SelectNextRound(1)` fell back to the first Arts / Point Builder
  entry. Registering all 30 categories fixes native selection: the same test
  changes from 0 choices and repeated Point Builder to 2 choices, different
  category ids and subsequent FastestFingerFirst / TriggerFinger rounds.
  The user reported v7 looks correct in manual play. The automated probe has
  no contestants, so player-count restrictions affect its plan. The finale,
  all player counts and exactly four category choices need separate coverage.
- The 4-player plan starts with Point Builder, then Pass The Bomb, Fastest Finger
  First, Pie Fight, Point Stealer, Bet It and Final Countdown. Starting a fresh
  game with Point Builder is intentional; repeating it throughout a game is not.
- **v6 pictures:** raw media +0xE is the aspect-ratio enum, not a sheet flag.
  Set it to 4 (16:9), and fit images in 640×360 before storing them at 640×368.
  A synthetic encode/decode test verifies portrait, square and wide proportions.
- Speech: `.rsf` sources are copied, and any other audio (wav/mp3/ogg, KiP `.wem`) is encoded
  by our own ATRAC3 encoder. Questions without speech share one silent clip.

### ATRAC3 encoder (`tools/questions/at3enc.py`, needs numpy)

This is our own implementation, so no third-party encoder is needed. It produces mono 48 kHz,
152-byte frames: the format of the game's question speech.
- **Synthesis model:** mirrors FFmpeg's decoder exactly (QMF tree with band order 0/1 and 3/2,
  odd-band reversal, 512-point IMDCT, the decoder window, overlap-add). The analysis is its inverse:
  the transform round trip is 75.5 dB.
- **Coding:**
  - CLC mantissas (selectors 1/3/5/6/7) and scale factors `2^((i−15)/3)`.
  - Allocation binary-searches a per-frame noise threshold to fit 1,216 bits, vectorised across frames.
  - No gain control or tonal components.
- **Decoder conventions it follows:**
  - IMDCT polarity is inverted vs the textbook basis.
  - FFmpeg derives the band count from the *start* of the last listed subband. When that
    subband begins a QMF band (0/18/26/30), one uncoded subband is appended.
- **Verified with FFmpeg's decoder** on Polish KiP speech: gain 0.994, SNR 20.1 dB, segmental
  21.6 dB, 1,162-sample (24 ms) latency.
- KiP voice gets +4.5 dB to match Buzz's speech loudness (peak stays below −4 dBFS).
- In-game decoding goes through the firmware ATRAC decoder lifted in the port. That is the
  remaining check.

Registration regression checks (synthetic fixtures, no game assets):
`python -I -m unittest discover -s tests -p test_buzzpack.py -v`.
These cover the native category range, collisions, parent/child references,
pack-specific lookup names, round indices and decoded question/answer text.

## Installation and discovery

**How to install a custom pack:**
`runtime/dev_hdd0/game/BCES00647/USRDIR/PACKnnnn/<LANG>PACKnnnn.edat`.
- **Name:** the archive must be `<LANG>PACKnnnn.edat` (no `.PAK`). The game ignores
  `<LANG>PACKnnnn.PAK` on the HDD; it only accepts DRM names there. The `.PAK.edat`, `.sdat`
  and `.PAK.sdat` variants are probed but not mounted.
- **Content:** a plain stored ZIP. The runtime passes non-NPD `.edat` files through as
  plaintext, and its `sceNpDrmIsAvailable` reports the content as available.
- **Discovery:** the boot scan lists `/dev_hdd0/game/BCES00647/USRDIR` (and `BCES00195`)
  and adds every `PACK*` directory to the search paths. The pack archive is opened after the
  language choice, together with `POL.PAK` and the disc's `POLPACK0100.PAK`.
- **Registration:** `RunPackageRegistrationScripts` (frontend1, after network init) runs
  `SCRIPTS/register` for every mounted pack.


### DLC discovery, from the recompiled EBOOT
- `func_00265AC0` (ppu_recomp_013.cpp) is the pack scanner. For each entry
  of a directory list it calls `cellFsOpendir`/`cellFsReaddir` and keeps entries
  with `d_type == 1` (directory) whose name starts with `PACK` (strncmp 4). It
  formats `"<name>/"` and registers the pack (`func_00264AF8`, `func_00261700`).
  `func_00266E80` is a second pass over the same list.
- Call chain: boot `func_00255C10` (HDDGameCheckThread / PRODUCT.DAT) leads to
  `func_0024FDC0` (only if `*(0x8ED86C) != 0`), then `func_00267190`, which calls
  the scanner (only if the list at `0x8ECDE8` is non-empty).
- The same boot loop walks that list and runs `cellGameDataCheckCreate2` per entry.
  The log shows `BCES00647` and `BCES00195`. So packs are expected under
  `/dev_hdd0/game/<title>/USRDIR/PACKnnnn/` (host:
  `runtime/dev_hdd0/game/BCES00647/USRDIR/PACKnnnn/`).

## Category and round registration

Category IDs must be in 1..999: the native binding rejects values outside
that range. Default custom ranges start at `500 + (pack_no - 101)*100` for
packs 101..105. `pack.category_base` can select another unused range;
co-installed packs require disjoint IDs and pack-specific internal names.
Display text keys remain separate.

RoundSetup must register subcategories as well as subject roots. The chooser
can select a subcategory, and the native round lookup needs a record for that
exact ID. Missing records leave zero choices and can repeatedly select the
first round. Each category record references its containing subject file.

## Limitations and debugging

The PS Eye photo scene (`eyetoyscene_onyourmarks`) can hang custom-pack games:
speech can remain active after `StartCamera`. `PS3_CAMERA=0` is a diagnostic
workaround, not a root-cause fix. Keep manual full-game/finale, player-count
and speech-quality validation distinct from a successful question display.

For EU 1.00 Lua calls, `PS3_CALLTRACE=40000000` with
`PS3_CALLTRACE_FROM=4829D8` traces `luaD_precall`; `BUZZ_BOOT_TRACE=1` logs
script inclusion and `PS3_FSLOG=1` logs file operations. Use a binding map
from the matching executable. Capture through the renderer's explicit GPU
readback. Keep diagnostic packs/probes and save changes in an isolated runtime,
then restore the normal archive before manual play.

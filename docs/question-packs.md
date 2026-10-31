# Custom question packs: findings and plan

Status: investigation complete, nothing implemented in the game yet.
Target: BCES00647 1.02 (Polish disc). All findings come from read-only
inspection of the extracted disc, the Buzz! Quiz TV disc (BCES00305) and the
recompiled EBOOT. Byte-level details: [question-format.md](question-format.md).
Reference decoder/encoder: `tools/questions/buzzq.py`; Lua 5.1 BE disassembler:
`tools/questions/luadis.py`.

## Verdict

| Goal | Feasible? | Notes |
| --- | --- | --- |
| Read every question (text, answers, category, difficulty, media) | **Yes, done** | 6,173 POL questions decode; re-encoding is byte-exact |
| Write new round files | **Yes** | Huffman text, `.qak`/`.kaq` and indexes all rebuild byte-exact; not yet tested in game |
| New packs selectable by players | **Yes, the menu already exists** | Pack Game and Manage Quiz Packs appear when DLC packs are registered |
| Text, picture, video and music questions | **Yes** | Pictures are frames in `pics00N.sdi`, video is `.sdi`, music is `.rsf` |
| Question speech / per-language sets | **Yes, with an ATRAC3 encoder** | All audio is ATRAC3 at 48 kHz; one PAK per language |
| Import Buzz! Quiz TV packs | **Very likely as whole packs** | Same containers and codec; Quiz World's loader accepts the Quiz TV file version |
| Import Knowledge Is Power (PS4) | **Unknown until unpacked** | Fake-signed PKG; PFS needs a PS4 PKG tool; engine/data format not yet seen |

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
- **Open item**: hash used by `NAMEDTEXT/default.ndx` (string-key lookup) is not
  identified yet. It is needed for new pack titles, descriptions and category names. The workaround is
  to reuse existing keys, or hook the lookup in the port.

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
- The file is a fake-signed PKG (`drm_type 0xF`) whose PFS image is encrypted with
  the standard fake-PKG keys. A PS4 PKG tool (LibOrbisPkg PkgTool / shadPS4
  installer) can unpack it. Data format unknown until unpacked.
- The questions are 4-answer multiple choice, which maps onto Buzz records. Speech
  will not match Buzz's host voice, so plan on text-only or generated speech.

## Proposed tool: `buzzpack`

A Python CLI with an optional small GUI later. It works on an editable source format:

```
mypack/
  pack.toml               id, title, description, languages, category tree
  questions.csv|json      id, category, difficulty, type, text, answers[4], media, per-language text
  media/                  png/jpg (pictures), mp4 (video), wav/ogg (music, speech)
```

Commands:
- `buzzpack extract <PAK|ISO> -o dir`: game pack to source format (Quiz World, Quiz TV)
- `buzzpack build dir -o PACKnnnn/`: source format to `<LANG>PACKnnnn.PAK` per language
  (Huffman text, qak/kaq, generated Lua, ATRAC3 speech/music, SDI video/picture sheets)
- `buzzpack import-kip <unpacked dir>`: once the format is known
- `buzzpack validate`: round-trip and reference checks

Port side: a switch to enable custom packs directory, any DLC-path mapping or Lua
registration the scan needs, and a smoke test that boots, opens Manage Quiz
Packs and plays one round from a custom pack.

### Milestones
1. Trace DLC discovery, then re-register an unmodified **copy** of PACK0100 under a new
   pack id/dir and confirm it shows in Manage Quiz Packs. This proves the whole path in game.
2. Tiny text-only custom pack (10 questions, reused title keys) built by the encoder.
3. Picture/video/music questions; ATRAC3 speech (recorded or TTS) per language.
4. Quiz TV pack import.
5. NAMEDTEXT hash, for real pack titles and category names.
6. Knowledge Is Power import after unpacking.

## Milestone 1: second pack discovery (static work done; run pending)

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
- In the 2026-10-31 run, `cellFs` logged **no `Opendir` at all**, so the scan
  was not reached on that boot. Which gate is closed is the first thing the
  traced run must answer.

### Test pack
`tools/questions/make_test_pack.py` builds `PACK0101/POLPACK0101.PAK` from
the disc pack. It contains one round (`q_people`, 222 questions), its 222 speech files,
10 videos, 3 music clips and the 3 picture sheets. `AddPackInfo` has pack id/order 101 and
dir `PACK0101`, and the round header has pack id 101. Titles and keys reuse pack 100's strings.
`tools/questions/clu.py` reads and writes `.clu` (all 519 shipped scripts
round-trip byte-exact) and emits simple call scripts.

```
python -I tools/questions/make_test_pack.py <USRDIR>/PACK0100/POLPACK0100.PAK <outdir>
```

### Traced run checklist (after the Codex optimisation work is finished)
1. Copy `<outdir>/PACK0101` to `runtime/dev_hdd0/game/BCES00647/USRDIR/PACK0101/`.
   Remove it after the test so other runs are unaffected.
2. Launch in the background and muted, to the main menu.
3. stdout: `[cellFs] Opendir` lines show whether and where the scanner looks.
   stderr: `[fs] open ... PACK0101` shows whether the PAK (or a `.edat` name) is opened.
4. If there is no `Opendir`, read `0x8ED86C` and the list at `0x8ECDE8` from a live
   process (`tools/inspect/inspect-live-memory.py`). Then decide between
   satisfying the gate (cellGame/HDD state) and registering packs from the port.
   For the port route: run the scanner after boot, or use a Lua `AddPackInfo` injection
   via `frontend_policy.cpp`.
5. In game: Main menu > Pack Game, and Extras > Manage Quiz Packs, should list
   two packs. Then play one round of PACK0101 alone.

Copyright: tools and docs only. No game questions, audio or video are committed;
extraction happens locally from the user's own discs.

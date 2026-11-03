# Handoff: custom question packs for Buzz! Quiz World (continue from here)

You are continuing work on custom quiz packs (DLC-style) for the Buzz! Quiz World
static-recompilation port. Read this whole file first, then `docs/question-packs.md`
and `docs/question-format.md` in this worktree.

## Where things are
- Worktree: `D:\Other\BuzzRecomp\buzz-question-packs`, branch `feature/question-packs`
  (main checkout `D:\Other\BuzzRecomp\buzz-quizworld-recomp`, already merged/pushed up to
  commit c1d0368 / main 0929230).
- Implementation in `tools/questions/buzzpack.py`:
  - `Generated/RoundSetup` with Quiz World's native `AddGame` (**9 args**, corrected in v6) and
    `AddGamePlanRounds` (Knowledge-channel plans), plus `AddRoundNew(id, game, channel,
    roundType, categoryName, packDir, file)`;
  - the pack's own NAMEDTEXT (title, description and game names; hash solved:
    `crc32(key) ^ 0xFFFFFFFF ^ 0xFABCDEF7`, signed int32);
  - round-file indices 1..k for `AddRoundFile` and question round lists;
  - the Polish title "WIEDZA TO POTĘGA";
  - **v5 category fix:** global ids 600..629 for pack 102, pack-specific
    internal names `p102_<name>`, and validation of registration references;
  - **v6 picture fix:** 16:9 media metadata and display-space letterboxing;
  - **v7 progression fix:** round entries for roots and subcategories.

  Also updated `docs/question-packs.md`, `docs/question-format.md`, this handoff,
  `tests/test_buzzpack.py` (synthetic regression tests), and `buzzq.py` (close
  read handles deterministically; no format changes).
- Tools: `tools/questions/` (buzzq, buzzround, clu, sdi, rsf, at3enc, buzzpack,
  kip_export, ps4pkg). `at3enc.py`/`buzzpack build` need numpy: venv `D:\Other\kip\venv`.
- Data (not in git): KiP export `D:\Other\kip\export\kip_pl_source.json`, KiP voice/images
  under `D:\Other\kip\extract` and `D:\Other\kip\images`, Quiz TV pack 18
  `D:\Other\kip\quiztv\POLPACK0018.PAK`.
- Current pack build: `D:\Other\kip\packs-v7\PACK0102\POLPACK0102.edat`, installed in **both**
  worktrees' `runtime\dev_hdd0\game\BCES00647\USRDIR\PACK0102\`.
  SHA256: `6AEC9A0A2B2D4DB527A7E15E375655C90A985AD1356070FA1107AA3D2E5EF074`.
  4,562 questions, 10 files, 30 categories, 304 pictures, 199.7 MB with voice.
  Previous installs are backed up in `D:\Other\kip\old-installs\question-categories-v5\`.
  Quiz TV pack 18 is installed there too, as `PACK0018\POLPACK0018.edat`.

Build:
```
cd D:\Other\BuzzRecomp\buzz-question-packs\tools\questions
D:\Other\kip\venv\Scripts\python.exe -I buzzpack.py build D:\Other\kip\export\kip_pl_source.json <game_root>\PS3_GAME\USRDIR\PACK0100\POLPACK0100.PAK <outdir> --pack 102
```
`<game_root>` is in `buzz-quizworld-recomp\buzz-paths.json`. A full build with voice takes about 4.5 min on 22 workers.

## Rules from the user
- Test launches: `-Background -Mute` unless the user wants to play. Never stop a game
  instance you did not start.
- Use the worktree's own `build-vulkan-windows` (a frozen copy of main's exe +
  `game-build.json`) and `runtime\` for automated tests. Install into the main runtime only
  for the user's manual tests.
- The user plays and tests with real buzzers (Polish UI). Profile creation and player joining
  need the user; scripted input (`BUZZ_SCRIPT`) gets as far as the pack list.

## What works (verified in game)
- HDD DLC discovery: a pack is `USRDIR\PACKnnnn\<LANG>PACKnnnn.edat`, which is a plain stored
  ZIP; the runtime passes non-NPD .edat through.
- Registration: `register.clu` → categories + `AddPackInfo` → `Generated/RoundPackages`
  (which includes `Generated/RoundSetup` first).
- The pack is listed under Zagraj w grę → Gra z pakietami quizów with its own title and
  description. Selecting it starts the show.
- Players join (profiles needed). With **`PS3_CAMERA=0`** the round starts.
- **User confirmed v5:** the question and all four answers display, and picture
  questions load their pictures (Poe portrait screenshot supplied).
- **User reported v7 looks correct (2026-11-03)** after the progression fix and
  requested commit/push. This is a successful manual smoke test; all player
  counts, the finale and exactly four category choices have not been separately
  confirmed.

## v6 follow-up (2026-11-03, historical)
- **Superseded by v7 below:** user confirmed improved pictures, but round 2
  still repeated Arts / Point Builder. Nine arguments are necessary, but
  were not the root cause of that repetition.
- User reported **Arts and Literature and Point Builder repeating even across
  successive rounds**, not just on restarting the game.
- Native `AddGame` (`func_0014FB20`) consumes **nine** arguments. The earlier
  eight-argument claim was wrong. Match the shipped Knowledge games:
  `(name, id, kind, maxPlayers, 0, -1, 1, 0, 4)`. Without the penultimate zero,
  4 goes to a boolean flag and the ninth argument becomes 0 (choice count).
- v6 fixes this. Native boot/memory check `logs/20261103-124354.stderr.log`
  confirms all three custom game structs have flag8=0, choices=4 and plan
  entry counts 3/7/4 (1p/4p/8p). Test process 32028 was stopped; no user process stopped.
  This boot check did not establish progression; see the v7 fix below.
- Raw media +0xE (formerly `in_pics`) is the **aspect ratio**, copied to cached
  +0x3E by `func_000DAE98`, returned by `func_000DB098` and mapped by
  `GetQuestionResourceAspectRatio` (`func_00139318`):
  1=4:3, 2=2:3, 3=1:1, 4=16:9, 0=none. The old 1 marked wide sheets 4:3,
  visibly narrowing portraits. v6 uses 4, fits in 640x360 display coordinates,
  then stores 640x368 to compensate for the scene's mapping.
- Four pack tests and `tests/test_sdi_aspect.py` pass. The latter actually
  encodes/decodes synthetic portrait, square and wide shapes and checks their
  final display proportions. Full v6 validation passes; ZIP CRC comparison
  against v5 confirms voice/text unchanged, only picture sheet, media databases
  and roundsetup changed.
- Local v6 build helper: `analysis/question-debug/build-v6.py`, using cached v5
  RSFs to avoid re-encoding speech. Regular `buzzpack build` remains reproducible.
  Manual launch: `D:\Other\kip\packs-v6\Test-Pack.ps1` (main runtime, camera off).

## v7 progression fix (2026-11-03)
- Root cause: RoundSetup registered only the 10 subject roots. The native
  chooser selects subcategories too, then `func_0010D668` searches game round
  records for that exact category id at +0x6C. Missing subcategory records
  left zero choices. Selecting choice 1 then fell back to record 0, hence
  Arts and Literature / Point Builder on every round.
- Builder now registers all 30 categories, each referencing its containing
  subject file. The AddGame v6 signature and pictures remain unchanged.
- Native A/B reproduction: inject a diagnostic Generated/Probe script into
  **only the worktree runtime**. Use loadstring bytecode (CALL C=2 then CALL
  returned closure) to run Tickets.CreateGameStartTicket("GAMEPLAN_PACK_4P",
  "p102PackGame4p", false), Tickets.ProcessGameStartTicket(), StartGame(),
  SetFirstRound(), then PrepareForNextRound(false,false),
  ProcessSelectedRounds(), GetNumberOfRoundChoicesAvailable(), SelectNextRound(1)
  in a loop. Test launches remain Background/Mute with camera off.
- Before: `logs/20261103-130402.stdout.log`, 0 choices and repeated PointBuilder.
  After: `logs/20261103-131236.stdout.log`, 2 choices and PointBuilder,
  FastestFingerFirst, FastestFingerFirst, TriggerFinger, FastestFingerFirst.
  Native history contains selected category ids 612, 629, 609, 605 (several
  subjects). There are no contestants in this probe: it filters round types
  by player count and removes the finale, so it does not verify a full game.
- Five pack tests pass, including subcategory-to-file registration coverage.
  Full v7 validation passes. ZIP CRC comparison against clean v6 confirms
  **only** POL/SCRIPTS/GENERATED/roundsetup.clu changed. Text/audio/images are
  identical. v7 was written with the normal aligned stored-ZIP writer.
- Diagnostic processes were stopped (only our PIDs). The temporary probe
  archive was removed by installing clean v7 in both runtimes. No probe is
  present in the installed pack. Manual helper:
  `D:\Other\kip\packs-v7\Test-Pack.ps1`.
- User subsequently reported that v7 looks correct and requested commit/push.
  Keep the automated probe's limited coverage distinct from that manual result.

## Known issues
1. **PS Eye photo scene hangs pack games** (`eyetoyscene_onyourmarks`): after speech
   802413700 + `StartCamera`, `IsAnyoneSpeaking` stays true forever. The runtime reports a
   camera by default (`ps3recomp/libs/hardware/cellCamera.c`, `PS3_CAMERA=0` disables it).
   Normal games pass this scene. Not root-caused. Options: make `PS3_CAMERA=0` the Buzz
   default (scripts/run-windows.ps1 + launcher), and/or find why the speech never ends in
   pack mode.
2. **Empty questions: fixed in v5, confirmed by the user.**
   A judgement loop does **not** prove a real question was selected. In session
   113042, `GetNumberOfAnswers` is called but there are no subsequent answer-bar
   setup calls: the current question is effectively empty.
   Root cause found in `AddQuestionCategoryItem` (`func_0013C6E8`): at 0x13C744,
   unsigned `(id - 1) <= 998` is required. Our old ids 10201..10230 were all
   silently rejected. Category names resolve globally (`func_000E28F0`), so
   v5 also uses pack-specific names in registration and `AddRoundNew`.
   The disc has 295 categories but the highest id is 436 (not 295).
   Default custom blocks now start at `500 + (pack_no - 101)*100`, supporting
   packs 101..105. `pack.category_base` can pick another free range within
   1..999; co-installed packs need disjoint ranges. Display keys still use CAT_*.

   **Native A/B verified:** v4 registration leaves the global registry at 295
   entries with no p102 names. v5 gives 325 entries and all 30 p102 names mapped
   to 600..629. Isolated background/muted test logs: worktree
   `20261103-114443` (v4), `20261103-114538` (v5). Both test processes were stopped.
   Read-only memory evidence is under `analysis/question-debug/registry-v{4,5}.json`;
   local inspector `analysis/question-debug/inspect-registry.py` takes our test PID.
   Category manager is `vm_read32(0x8759D8-0x5DC0)` = 0x8C5B40 in eu100;
   id-map count +8, head +4; internal-name map head +0x54.

   **No text or media changes:** compared ZIP CRCs against v4. Only the 20 qak/kaq
   files (category ids/indexes) and questioncategories/roundsetup scripts differ.
   `text_no` remains 0 to isolate this fix. If v5 is still empty, inspect the
   current cached question and revisit these older hypotheses:
   - `text_no` (question record offset 8) is 0 in our files. Quiz TV numbers it 1, 2, 3, 4,
     6, … (source ids) and Quiz World uses DB ids like 3157. 0 may mean "no text". Try
     sequential non-zero values.
   - Check whether the Quiz TV pack 18 shows text in a pack round (with PS3_CAMERA=0). If
     yes, diff its round files against ours field by field (`buzzq.parse_round`).
     Differences already seen: QTV version 1002, header u16 at 0x10 = 1 (ours = number of
     rounds), categories `[115]`, and each question lists every round-file index it appears
     in.
   - The text might be looked up through the category or round file. Check what
     `AddRoundFile(n, 0, qfile, tfile)` arguments 3/4 mean, and whether pack rounds resolve
     files through `AddRoundNew`'s file argument.
   - The string table itself decodes and round-trips byte-exact with our encoder (all
     shipped files), so a codec bug is unlikely.

## Debug method that works
Lua→C call trace, cheap (~600 calls/s):
`PS3_CALLTRACE=40000000` and `PS3_CALLTRACE_FROM=4829D8` (luaD_precall in the eu100 build),
plus `BUZZ_BOOT_TRACE=1` (IncludeScript). Decode targets with the binding map
`D:\Other\kip\trace\lua_bindings.json` (address → Lua name, 2105 entries). Other useful
switches:
- `PS3_FSLOG=1`: fs opendir/stat/open;
- `PS3RECOMP_VK_DUMP=<file.ppm>` + `PS3RECOMP_VK_DUMP_EVERY=30`: frame capture (copy the file
  periodically from a watcher job).

Logs: `logs\<stamp>.stderr.log`. User sessions: 20261103-112403 (photo hang),
20261103-113042 (camera off, empty questions).

Manual test helper (local, not in git): `D:\Other\kip\packs-v7\Test-Pack.ps1`.
It launches the **main checkout** normally with PS3_CAMERA=0, boot/fs/Lua-call
traces, and restores the caller's environment afterward. v5 visibility is confirmed;
v6 picture proportions are confirmed improved; the user reported v7 looks
correct in manual play after the progression/category fix.

Validation completed: `_validate` on the full v7 archive (including SDIs,
Huffman tables, swapped databases, category/speech references), five synthetic
pack tests, the FFmpeg picture-aspect test, byte-exact writer selftest on all
11 original POL rounds, native A/B progression probes, and `git diff --check`.

## Next steps
1. Broaden manual coverage to the finale, 1p/8p and exactly four category choices
   (launch with PS3_CAMERA=0); the current manual smoke test looks correct.
2. Check voice quality (our ATRAC3 `.rsf` via the game's own decoder).
3. Decide on the PS3_CAMERA default / the photo-hang root cause.
4. Merge to main when the user asks; current commit/push is on feature/question-packs.

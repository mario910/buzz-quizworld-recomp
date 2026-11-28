# Buzz! Question Studio

Native C++17 / Qt Widgets desktop editor for Windows and Linux. Version 0.3 adds
native Quiz World EDAT building and installation, using the selected content language.
It builds independently of the game, PS3 SDK and generated game code, and uses
the same Qt libraries (Core, Gui, Widgets) as BuzzSetup. It does not need Python.

## Start

Release packages (BuzzSetup) include Question Studio:

- **Windows:** in BuzzSetup select **Question Studio**, or start
  `BuzzPrepare\BuzzQuestionStudio.exe` (next to BuzzSetup, sharing its Qt DLLs).
- **Linux:** select **Question Studio** in BuzzSetup, run `./Question-Studio.sh`
  from the extracted package, or `BuzzSetup-x86_64.AppImage --studio`.

When BuzzSetup opens Studio for a prepared game, it passes that folder as
`--game-dir <dist>`. The build dialog proposes that copy's `runtime`, reads the
title ID from its disc's `PARAM.SFO`, and looks for the PACK0100 template in the
selected language.
Installation works before the game's first start. The interface uses the same
colours and controls as BuzzSetup.

## Import questions

Choose **Import** to start the three-step wizard:

1. Choose a disc image or question file. For an extracted PS3 or Knowledge Is
   Power game, choose its folder. You can also paste a path or drag the source
   into the wizard. For an extracted PS2 or PSP game, choose its `ROUND.PAK`
   or `MERGED.QDB` file.
2. Choose from the detected question languages and decide whether to add the
   questions to your current project or open them as a new project. For paid
   PS3 DLC, the wizard asks for the `.rap` license if Studio cannot find it.
3. Review the question totals, reasons questions were skipped and the original
   rounds in which repeated questions appeared. If images or clips can be
   recovered, **Recover game media** attaches them directly to their questions.
   The wizard reports what is still missing and lists unsupported standalone
   music separately. **Skip and go to the editor** leaves media recovery for later.

Studio keeps a game's source categories when importing questions. A source
section becomes a project category, with deeper levels combined into a
subcategory name to fit Quiz World's two levels. Compatible base Quiz World
categories retain their existing mapping. If the game supplies no category
name or the project reaches its category limit, Studio reports the affected
questions and keeps their source information.

Use the category assignment button in the summary to remap a source section
and its subsections to one Quiz World or custom category and subcategory.
The summary limits changes to newly added questions. The same action in the
main menu can map sections across the entire project. Each assignment is
one undoable change and keeps the original section available for later use.

Use **Open** to resume a saved Question Studio project.
New projects start in the interface language, which follows the system until
the user chooses another language. The example opens in the selected question language.

## Implemented

- Three-pane interface in the Buzz! game languages (Polish, English, German, French,
  Italian, Spanish, Dutch, Portuguese, Swedish, Danish, Norwegian, Finnish, Russian,
  Czech, Greek and Croatian). Switch it live under **… → Język interfejsu / Interface
  language**. On the first start Studio uses the system language and falls back to
  English. The preference is remembered independently of project data.
  Includes translated controls, validation, status messages and import errors.
- Category/text/media filters and a virtualized list.
- Create, edit, duplicate and delete questions; choose the correct answer and
  difficulty; map questions to Quiz World subjects/subcategories or to the
  project's own (see [Custom categories](#custom-categories)).
- Question preview with four coloured answers and aspect-preserving images.
- Question cards show text, image or video as the content type and indicate
  narration separately. The **With narration** filter includes any content type.
- Attach images, videos and narration files. Narration opens in the system's associated player;
  this is not yet an internal RSF/WEM player.
- Generate question audio locally as WAV, with voice selection, speed and
  pronunciation text: Windows SAPI voices on Windows, eSpeak NG on Linux.
  Generation runs off the GUI thread, writes atomically, then attaches the WAV
  as an undoable edit. Existing audio is preserved if generation fails.
- Undo/redo, unsaved-change prompts, atomic project saves and a recovery copy
  every 30 seconds while the project is modified.
- Open current buzzpack JSON, Question Studio JSON, or exported KiP JSON.
  The import dialog detects languages and lets the user choose; it preserves
  all KiP translations, including their correct-answer indices and media metadata.
  Questions without the selected source language are skipped, not imported as drafts.
- Native import of Knowledge Is Power / Wiedza to Potęga (PS4, CUSA09557) straight
  from the fake-signed `.pkg` (outer PFS decrypted with XTS-AES, compressed inner
  PFS read in place) or from an extracted folder (`Media/resources.assets`).
  Reads the `KIP_*` question tables, offers locales with translated questions,
  and imports the `QuestionImages.bytes` pictures and available `.wem`
  narration for each language version kept in the project.
  AES comes from the system (CNG on Windows, OpenSSL libcrypto on Linux).
  Sorting and linking with usable answer sets are converted to single-choice
  questions. Studio reads category names from the game's localisation tables,
  keeps the full source path, and uses its first three levels to group questions
  within Quiz World's category limit.
- Native read-only import of stored ZIP PAK / plaintext EDAT question archives
  from Quiz World and Quiz TV. Parses Huffman text, QAK records and category
  metadata; never executes imported Lua bytecode. Imported speech is cached
  locally. Still-picture questions use the frame index in the QAK record to
  import the matching frame from the pack's SDI picture sheet as a cached PNG.
  This uses the format of the pack, not a list of game IDs or languages. Use
  FFmpeg next to Studio, on PATH or selected in the Studio menu. If FFmpeg
  is missing or a sheet cannot be decoded, the questions and their original
  references are still imported with a warning. Use **Decode source media**
  on an imported movie question to read its original SDI, or use **Recover game
  media** for the whole project. Import from a decrypted PS3 ISO or question
  pack copies music automatically. On older projects, **Play** can recover just
  the selected question’s clip before opening the player. Studio stores the
  clip separately from narration and includes it as streamed music in Quiz World packs.
  Copying music works without FFmpeg. Questions whose clips could not
  be recovered report the missing music and are skipped by default at build time.
  Use **With music** to see all questions that use a music clip, including
  questions still waiting for recovery. **Play** beside music, narration or
  video opens a player in Studio. Previews of RSF and WEM use FFmpeg.
  Multiple correct text answers can become one correct combination. Picture
  answers and questions without a usable answer set are skipped with a count.
- PS3 sources are read in place without unpacking the whole disc or package:
  - Decrypted disc images (`.iso`) of Quiz TV, Quiz World, Brain of..., and The Ultimate Music Quiz: every
    `PS3_GAME/USRDIR/PACKnnnn/<LANG>PACKnnnn.PAK` in the chosen language is
    imported at once. An image whose files are still encrypted is reported.
  - PSN packages (`.pkg`, e.g. the Quiz TV Comedy/Horror/Sci-Fi/Video Games
    packs): the package is decrypted with the published package key and its
    NPDRM `.EDAT` packs block by block, each block hash checked. Paid DLC uses
    your own `.rap` license. Studio checks the last `.rap` you chose, then looks
    for `<content ID>.rap` next to the package, next to an installed pack or in
    `dev_hdd0/home/*/exdata`. If none matches, Studio asks you to choose one and
    remembers its location. Game updates (no `PACKnnnn` folder) are recognised
    and refused.
  - Installed DLC (`dev_hdd0/game/<title>/USRDIR/PACKnnnn/*.EDAT`) and extracted
    games: open the `.EDAT` itself, or the folder's `PARAM.SFO` to import all of
    its packs. The title comes from `PARAM.SFO`.
  The RAP conversion and NPD constants are the published ones also used by the
  runtime (`ps3recomp/libs/filesystem/edat.c`); no console- or buyer-specific key
  is included. EDATs locked with a game's own key (data in game updates) are
  not question packs and are refused.
- Native read-only import from the PS2 and PSP games, straight from a disc image
  (`.iso`, PSP `.cso`) or from the extracted files:
  - PS2 The Music Quiz, The Big Quiz, The Mega Quiz, Hollywood Quiz, The Pop
    Quiz, The Schools Quiz and The Sports Quiz. Studio reads the media links
    from the game's own round tables. It supports RSV pictures and PAL/NTSC
    video, Sports Quiz's separate image and indexed IPU archives, and VGP
    music and sound clips. Sports Quiz's question narration is available in
    its own player. Questions with four picture answers need a compatible
    replacement to build as single-choice Quiz World questions.
  - For the Polish Mega Quiz (SCES-54690), Studio applies the verified picture
    offset correction to the original `rri.dat`. Australian Big Quiz v2.00
    also has a verified correction for the picture sheet's ZIP checksum.
  - PSP Master Quiz, the regional Brain quizzes, Quiz World (UMD and PSN),
    and The Ultimate Music Quiz. Studio detects the languages present on the
    disc and attaches imported pictures and music clips. **Play** recovers the
    selected film or narration on demand. Studio combines the original PSP
    video and soundtrack in an MP4 and uses the original narration when
    building the pack if the source is still available. Brain Bender contains
    puzzle minigames and has no quiz question bank to import.
  The correct answer is stored first in these games. Studio keeps the category
  names it can read or identify from the source. Comparison and true/false
  rounds with identifiable correct
  answers are converted to the four-choice format. An ordering table that labels
  the first item can be converted to asking which item comes first. Rows without
  a usable answer key and tables left in another language are skipped.
  Select an imported question and click **Play** next to its video. Studio
  uses FFmpeg to recover that clip, attaches it as an MP4 and plays it in the
  editor. When the source clip includes its own audio, the MP4 includes it.
  The **Play** buttons for narration and music can also recover their own
  source audio on demand. When building a Quiz World pack, Studio encodes
  recovered music and sound clips as stereo RSF and stores narration separately.
  **Recover game media** tries missing images and clips in the current project.
  It reuses shared images and clips, keeps existing attachments and edits, and
  allows you to undo the recovered attachments together. Build Pack opens a
  list of questions that still need media, with an option to recover them or
  open a question to attach a replacement. The pack builder opens once recovery
  clears the list.
  The Quiz World pack builder plays the attached clip with its own audio.
  If Studio cannot find FFmpeg, **Recover game media** offers to install it
  through WinGet on Windows, APT, DNF or pacman on Linux, or Homebrew on macOS.
  Studio checks the installed executable and resumes recovery. You can also
  select an existing executable or open the [FFmpeg download page](https://ffmpeg.org/download.html).
  Linux installations use the configured repositories. Studio offers a terminal
  command when graphical authorization is unavailable and a manual executable
  on systems such as SteamOS.
  If an existing MP4 looks scrambled into blocks, remove the old video and
  its generated still in the editor, then run **Decode source media** again and
  rebuild the pack to use the corrected macroblock order. Unsupported
  media keep their original references for manual replacement. Older imports
  need to be reimported if they lack video references.
  PSP Brain Bender has puzzles only.
- Import into the current project with identity-based duplicate handling.
  Importing another language of the same Buzz question merges its translation;
  existing text/media is preserved and entirely empty drafts can be filled.
  Reimporting can supply missing original narration for a matching question
  and language whose prompt has stayed the same.
- Export JSON for the existing buzzpack CLI, moving the correct answer first.
  That export requires complete questions and valid template category mappings.
- Build aligned, stored-ZIP plaintext `.edat` packs directly in the application:
  native QAK/KAQ, Huffman text, CLU registration, NAMEDTEXT, SDI picture sheets and
  ATRAC3 speech. No Python process is invoked. Media conversion uses FFmpeg.
- Install a built or existing custom EDAT into a selected runtime, checking
  categories against installed packs and backing up an existing version before
  an atomic replacement. Build/install runs in the background and can be cancelled.

## Build and install a pack

1. Select the content language in the header, complete its questions and category
   mappings, and attach images/audio or generate WAV with **Generate speech**.
2. Click **Build an .edat pack…**. Choose the original Quiz World **PACK0100** PAK in
   that language, an output directory, and a custom pack number (default 103).
   For Polish, use `POLPACK0100.PAK`; English needs an English template. A template
   for another language cannot supply translated category names. Project language,
   not interface language, controls the resulting pack.
3. Review the preliminary counts for ready, incomplete and empty questions in
   the active language. Click **Needs fixes** to see the questions and their
   reasons. Open a question in the editor, move through the list with
   **Previous question** and **Next question**, then use **Back to build** to
   keep your settings and refresh the counts. Studio checks the template and
   decodes media during the build.
4. Choose **Build .edat** or select the game's runtime folder (the folder containing
   `dev_hdd0`) and click **Build and install**. Verify the title ID when you choose
   a runtime folder manually. The completion dialog shows generated, installed,
   and backup paths.
   Existing packs can also be installed using **… → Install an .edat pack…**.
5. Restart the game and choose the pack in the quiz-pack game mode.

Output is `PACKnnnn/LANGPACKnnnn.edat`. Only the selected language is emitted.
Studio skips incomplete questions, invalid category mappings, and questions
with missing or unreadable media. The build report lists excluded questions
and their reasons. Select an entry to open the question in the editor. Empty
translations for the selected language are counted separately. The project keeps
all questions, and a question can be included after fixing it and rebuilding.
If every question is excluded, Studio preserves the previous output.
Images retain their proportions in a 16:9 letterboxed canvas.
An attached video takes precedence over the still image. For short clips (up to
30 seconds, with an audio track), FFmpeg supplies the MPEG-1 video and stereo
48 kHz ADX audio. Studio writes a native `.sdi` movie using metadata from the
selected Quiz World `PACK0100` template. A template without a compatible movie
header reports an error rather than silently substituting a still. The
converted media are inside the installed pack; the game does not need FFmpeg.
Supported audio includes WAV/generated TTS, RSF and KiP ATRAC9 WEM; FFmpeg decodes
foreign audio and the native encoder writes ATRAC3. Successfully encoded speech
is cached by content and gain. Imported native SDI/video/music references require
conversion or a replacement image or video; they are never silently dropped.

In **Advanced settings**, select FFmpeg if it is absent from the application
directory and PATH. Change the category base if the installer reports a collision:
custom category IDs must remain disjoint and fit 500..999. Defaults for packs
101..105 use bases 500, 600, 700, 800, 900. The actual category count also has to fit.
Supported title IDs are BCES00647, BCES00644, BCES00645 and BCUS98209.
The installer writes only the selected language's custom pack (number >=101),
and keeps backups under `runtime/pack-backups/<title>/PACKnnnn/`. Cancelling a
replacement leaves the prior installed file intact. Builds use a document snapshot;
edits made while building belong to the next build. Building does not save the project.

## Custom categories

**… → Kategorie paczki…** (or **Własne kategorie…** next to the category picker)
creates the project's own subjects, such as "Harry Potter" or "Christmas", each
with one or more subcategories. Every category has a name per project language,
up to 40 characters. Own subjects appear after the Quiz World ones in the
question's category picker. Removing a subject leaves its questions without a
category; the change can be undone.

When building, each own subject becomes a subject of the pack with its own round
file, next to any Quiz World subjects the pack uses. Its name goes into the
pack's NAMEDTEXT under a pack-specific `CAT_PK<number>_OWN<n>` key, which the
category chooser displays. Internal names are `p<number>_OwnA`, `p<number>_OwnB`, …
Imported categories use their source name when no translation is available.
A manually created category still needs a name for the language being built.
Own and Quiz World categories share the pack's limit of 99
categories in the category base range. Own categories have not yet been checked
in the game's category chooser with players; the buzzpack JSON export refuses
them.

## Round plans

Open **… → Round plans…** to edit the solo, up to four players, and up to eight
players plans independently. Select a round to change its type or question
count, move it, add another round after it, or remove it. **Default** uses the
pack's usual question count for that round. Some rounds can end early under
their normal rules. Each mode needs at least one round.
**Restore defaults** resets the selected mode. Studio saves the plans in the
project and uses them when building an `.edat` pack.

## Project format

Save projects as `name.buzz.json`. The root has `format: "buzz-question-studio"`,
`version: 2`, `pack`, and `questions`. Version 1 projects are read and migrated.
Each question keeps its stable id, four
answers in editor order, explicit zero-based `correct_index`, and unknown source
metadata. Legacy JSON without `correct_index` defaults to answer zero.

`pack.categories` (optional) lists the custom categories as `{id, parent, names}`:
ids from 10001, `parent` 0 for a subject or the id of one of the project's
subjects, and `names` keyed by language code. Imported categories also keep a
source key and fallback name. Questions refer to them through
`subject`/`category` like Quiz World ids. `pack.titles`/`pack.descriptions`
(optional) hold per-language titles; `pack.title` is the active language's.

`pack.language` selects the currently edited language; `pack.languages` lists
the project's languages. Each question's `translations` object is keyed by
language code and stores question text, four answers, correct index, topic,
image, audio and pronunciation text. Flat fields mirror the active language for
compatibility. Original per-language metadata is retained within each variant.
Buzz identities include their QAK/game version so Quiz TV and Quiz World IDs
cannot accidentally merge. Studio 0.1 Buzz IDs are migrated using source metadata.

Click the **PL / EN / …** button in the header, or **… → Pack languages**, to
switch the edited version or add a language. Common language choices are supplied;
other language/region codes, such as `en-GB`, can be entered. Adding a new project
language creates empty drafts for authoring; importing sources skips questions
without the selected language. Neither operation substitutes another language. Switching
content language is undoable. Category mappings are shared; the pack title (and
description) is kept per language: renaming changes the edited language's title,
and a language without its own title uses the current one until renamed.
The interface language does not translate authored questions or change their data.

For Buzz archives, only the selected language is read; import the archive again
with another language to merge it. Native folder codes (POL/ENG/FRE/GER/etc.) are
normalized to editor codes. Missing requested languages fail explicitly. Source
media references remain language-specific, even where previews are unavailable.
The existing buzzpack JSON export emits **only the active language** and requires
its questions to be complete; saving a Studio project preserves all languages.

A project is a **folder**: the `.buzz.json` file and the media it uses,

```
Moja paczka/
  Moja paczka.buzz.json
  media/images/<name>-<hash>.png       pictures
  media/audio/<language>/<name>-<hash>.wav|rsf|wem   speech per language
  media/video/<name>-<hash>.mp4        videos
```

Saving the project (**Zapisz projekt** / **Zapisz jako…**) copies every picture,
recording and video that still lives elsewhere into `media/` and relinks it:
files you attached from other folders, generated speech, and speech imported
from games (which starts in the application cache). Files already inside the
project folder stay where they are. The name keeps the original file name plus
the first 8 hex digits of its SHA-256, so the same file is stored once and a
second save copies nothing. The JSON holds only relative `/` paths, so the
folder opens the same on Windows and Linux. Links to files that no longer
exist are kept and reported (status bar and per-question problems).

Each question (and each language variant) has `image`, `speech` and an optional
`video`. Video is kept with the project and opened in the system player.
The pack builder uses the video and its audio if attached; a poster image
is optional.

**… → Eksportuj projekt z mediami (.buzzproject)…** writes the project and all
its media into one file (a ZIP: the `.buzz.json` at the root plus `media/`).
Opening a `.buzzproject` unpacks it into a new folder next to it (an existing
folder is never overwritten; a second copy gets a ` (2)` suffix) and continues
from there; later saves go to that folder. Archives may contain only the project
file and `media/` paths.

Recovery/cache/settings use Qt's platform-specific standard paths. No user or
machine-specific paths are compiled into the application. Imported source files
are not modified. Opening a non-Studio source requires Save As to create a project.

## Build on Windows

Install Visual Studio 2022 C++ tools, CMake 3.21+, Ninja, and Qt 6.2+ for MSVC x64
(Qt Base, Qt Multimedia and Qt Tools/Linguist). Qt 6.8.3 was tested. zlib comes from the
`third_party/zlib` submodule.
From PowerShell:

```powershell
.\scripts\build-question-studio.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\Launch-Question-Studio.cmd
```

If using the repo's private CMake/Ninja toolenv, set `BUZZ_TOOL_ENV` to its root.
The script builds, runs tests, installs into `dist-studio/`, and uses windeployqt
to collect Qt's runtime dependencies (no software OpenGL, DXC or VC++
redistributable installer) plus the Visual C++ runtime DLLs. The release workflow
places `BuzzQuestionStudio.exe`, its image plugins and the Windows playback backend
next to BuzzSetup in `BuzzPrepare/`. It publishes the Qt Base and Qt Multimedia
source archives. Running the resulting application needs
neither Python, PowerShell nor a development Qt installation.
For images or audio conversion, install FFmpeg, select its executable in the build
dialog, or include an actual standalone executable using the build helper's
`-FFmpeg C:\path\to\ffmpeg.exe` option. Do not pass a package-manager shim when
preparing a portable directory. FFmpeg is a separate licensed dependency.

## Build on Linux

Ubuntu 22.04/24.04:

```sh
git submodule update --init third_party/zlib
sudo apt-get install build-essential cmake ninja-build libssl-dev qt6-base-dev qt6-multimedia-dev \
  qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools ffmpeg
cmake -S studio -B build-studio/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-studio/linux --parallel
ctest --test-dir build-studio/linux --output-on-failure
./build-studio/linux/BuzzQuestionStudio
cmake --install build-studio/linux --prefix "$HOME/.local"
```

This installed Linux build uses the system Qt libraries. The BuzzSetup release
package and AppImage instead bundle Question Studio with BuzzSetup's Qt and the
image plugins it needs (`tools/package-preparation-linux.py`). X11 and Wayland use Qt's platform plugins. Automated
Linux verification runs in CI (Ubuntu 22.04/24.04) with the offscreen platform;
this does not establish Wayland compositor coverage.

Local TTS on Linux additionally needs `sudo apt-get install espeak-ng`.
On Windows, the voice list contains installed desktop SAPI voices (not all
OneCore/Narrator voices are necessarily exposed through SAPI). Choose a Polish
voice for Polish questions. Generating WAV neither calls a cloud service nor
requires an API key. Changing a question later does not regenerate its speech;
use **Generate speech** again. Undo detaches a generated WAV but keeps the saved file.

### Local voice cloning with OmniVoice

On 64-bit Windows and Linux (x86-64), open **… → OmniVoice…** and click
**Install OmniVoice…**. Review the download and disk space requirements and
confirm to start. Studio creates a private Python environment, installs
OmniVoice, PyTorch and FFmpeg, and downloads the model and speech codec. Allow
several GB of downloads and disk space on first setup. Installation works with
standard user permissions. After setup, synthesis runs offline.

Studio selects CUDA for eligible NVIDIA cards and ROCm for eligible AMD GPUs.
On Windows 11 it uses AMD's ROCm 7.2.1 wheels for compatible Radeon and Ryzen
devices. On Linux it uses the PyTorch ROCm 6.4 wheel for the GPU architectures
included in that build and requires access to the AMD kernel driver. The installer
tests an FP16 operation on the GPU and switches to CPU if GPU setup fails.
Choose **Reconfigure OmniVoice…** to detect hardware again after changing the
GPU or its driver. Studio shows the selected backend when setup finishes.
[Windows Radeon compatibility](https://rocm.docs.amd.com/projects/radeon-ryzen/en/docs-7.2.1/docs/compatibility/compatibilityrad/windows/windows_compatibility.html) ·
[Linux PyTorch wheels](https://pytorch.org/get-started/previous-versions/#v280)

For an existing environment, use **Choose Python…** with a Python 3.11 or newer
interpreter that has [OmniVoice 0.2.1](https://github.com/k2-fsa/OmniVoice#installation).
The OmniVoice code is Apache-2.0. The pretrained weights are for noncommercial
use under CC BY-NC. The Higgs Audio 2 tokenizer has separate Boson and Meta license
terms. Studio links both licenses before setup. [Third-party notices](../THIRD_PARTY_NOTICES.md#optional-omnivoice-narration).

1. Select the **Question language**. If you have no saved profile for it,
   Studio looks for a reference in the game prepared by BuzzSetup. It reads
   one narrated question in that language. To use a different reference,
   choose **Use imported narration** or select an audio file.
   A clean 3–10 second recording works well. Check that the transcript matches
   what the speaker says. Confirm that you have permission to clone the voice
   before generating narration, including with samples from the installed game.
   The FFmpeg installed with OmniVoice converts game formats such as RSF and
   WEM and can also encode generated narration when building the pack.
2. Use **Preview voice** to hear the current text. **Save profile** remembers
   your choice for that language on this computer and gives it priority over
   the automatic sample.
3. Use **Generate for question** for the current question or **Fill missing
   narration** for every question with text in the selected language. Existing
   files and narration recoverable from an available game source are kept.
   Studio uses a question's pronunciation text if it has one.

Batch generation reports progress and can be cancelled. Completed clips are
attached together as one undoable edit. WAVs become part of the project when
it is saved or exported, and the normal `.edat` builder encodes them for the game.

`--demo` opens authored sample questions. `--open <file>` opens a library.
`--inspect <file>` runs a headless importer and reports counts/languages/errors;
add `--language en` to select a source language.
`--smoke` builds the interface and fails on any Qt warning.
`--demo --screenshot <png>` captures the editor; add `--preview` for the preview.
`--demo --tts --screenshot <png>` captures the local voice-generation dialog.
`--ui-language en` sets a session-only interface override without changing the
saved preference. `--show-languages` opens the content-language dialog for capture.
`--show-build` opens the build dialog for capture; `--show-categories` opens the
custom-category dialog after `--open` has loaded the project.
`--game-dir <folder>` names a prepared game folder (the one containing `runtime/`);
BuzzSetup passes it so build/install suggestions use that copy.

Native command-line building uses the same implementation as the GUI:

```sh
BuzzQuestionStudio --build project.buzz.json --language pl \
  --template /games/QuizWorld/PS3_GAME/USRDIR/PACK0100/POLPACK0100.PAK \
  --out /home/user/BuzzPacks --pack 103 --category-base 700
BuzzQuestionStudio --install-pack /home/user/BuzzPacks/PACK0103/POLPACK0103.edat \
  --runtime /games/buzz-runtime --title-id BCES00647
```

Add `--ffmpeg <executable>` to override media conversion; add `--install --runtime
<folder>` to build and install together. Results are JSON with paths/counts and
SHA-256. In PowerShell, pipe the GUI executable's output to `Out-Host` to wait for
completion, or use `Start-Process -Wait` with redirected output.

## Remaining implementation stages

1. Decoded native media previews (SDI/RSF/WEM, video thumbnails).
2. Split PS3 images (`.iso.0`/`.iso.1`) and encrypted ones (`.dkey`); decoding
   additional PS2/PSP media formats.
3. Additional TTS adapters (Piper and HTTP services), internal audio playback,
   batch generation and a generated-audio cache. `src/tts.h` is the provider
   boundary; the first local Windows/Linux adapters already produce WAV.
4. Larger-library tooling, an in-game check of custom categories with players,
   additional interface translations
   and broader Linux desktop coverage.

Preserve the v7 pack rules: global category ids 1..999, disjoint installed-pack
ids, pack-specific internal category names, native nine-argument AddGame calls,
round registration for subcategories, and 16:9 image metadata/letterboxing.
Keep byte-exact reference fixtures and game smoke checks when changing the binary
writers. The compiled builder shares the v7 registration/progression rules.

## Verification

The same five CTest suites run on both platforms: project format/native fixtures,
controller/import/undo/local TTS, native pack builder (including synthetic ISO/CSO,
PS2/PSP, PFS/XTS and KiP imports), actual widget editing/dialogs and UI smoke. Builder tests compare every byte of a complete authored pack and
non-silent ATRAC3 output against frozen Python reference fixtures. FFmpeg tests
decode encoded audio (SNR >15 dB) and picture sheets (portrait proportions).
Installation tests cover backups, category collisions, invalid archives, cancellation
and symlinks escaping the selected runtime. UI tests click the build action and
verify the resulting selected-language EDAT. Controller tests exercise asynchronous
build/install/backup and cancellation.
Language tests cover KiP variant retention, native language discovery/selection,
missing-language source filtering and authored drafts, per-language answers/audio/media rebasing, v1 migration,
active-language export, identity merging and undo. UI tests switch PL/EN live,
verify unchanged saved content and exercise both import/project language dialogs.
The English TS catalog is checked for missing translations and altered format
placeholders/file filters.
The TTS integration test skips if no local voices are installed; Linux CI installs
eSpeak NG. Local Windows and Linux runs generated real RIFF/WAVE files, not mock
provider responses. Tests cover preserving an existing file when TTS fails.

Real-library checks: 4,562 questions from KiP JSON and the v7 EDAT; 489 supported
unique questions from Quiz TV pack 18, with explicit unsupported-type/media and
duplicate notices. Retail source files remain local and are not test fixtures.
The multilingual KiP source contains 6,263 question identities with EN/PL/RU
coverage; not every question exists in every language. Import skips questions
without the chosen language and reports their count. EN imports 4,763 questions
(1,500 skipped); PL imports 4,862 (1,401 skipped). Other translations of accepted
questions remain editable.

Studio 0.3 also built the full Polish KiP library with WEM audio and pictures:
4,562 questions, 10 round files, 30 categories, 304 pictures, 198,662,235 bytes.
The independent Python archive validator passes; every RSF matches the game-tested
v7 byte-for-byte. Linux also built a real KiP WEM/image sample with system FFmpeg 4.4.2.
A background/muted game boot opened the new pack from the test runtime. Full
round progression with players/buzzers has not been retested for this generated
pack; earlier v7 gameplay verification is not a new Studio gameplay test.

## Dependencies

Application sources use the repository's MIT license. Qt is a separate dependency
with its own module licenses; include the appropriate Qt notices and distribution
materials when distributing a runtime package. No game assets or questions from
retail games are included. Example questions and the icons are authored here.
zlib (bundled, zlib license) and the system AES library (CNG / OpenSSL) are also used.
Optional FFmpeg is executed as a separate program and retains its own license;
the applicable license depends on the selected build/configuration. The local
Windows development package uses a GPL-enabled FFmpeg build. Preserve its notices
and corresponding distribution materials when distributing that executable.

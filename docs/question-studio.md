# Buzz! Question Studio

Native C++17 / Qt Quick desktop editor for Windows and Linux. Version 0.3 adds
native Quiz World EDAT building and installation, using the selected content language.
It builds independently of the game, PS3 SDK and generated game code.

## Implemented

- Polish/English three-pane interface, switched live under **… → Język interfejsu /
  Interface language**. The preference is remembered independently of project data.
  Includes translated controls, validation, status messages and import errors.
- Category/text/media filters and a virtualized list.
- Create, edit, duplicate and delete questions; choose the correct answer and
  difficulty; map questions to Quiz World subjects/subcategories.
- Question preview with four coloured answers and aspect-preserving images.
- Attach images and audio files. Audio opens in the system's associated player;
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
  It is not a raw PKG importer.
- Native read-only import of stored ZIP PAK / plaintext EDAT question archives
  from Quiz World and Quiz TV. Parses Huffman text, QAK records and category
  metadata; never executes imported Lua bytecode. Imported speech is cached
  locally. Original SDI/music/video references are retained, not decoded yet.
  Picture-answer and multiple-correct-answer questions are skipped with a count;
  their indices are not misinterpreted as text. Only one correct answer out of
  four text answers is currently supported.
- Import into the current project with identity-based duplicate handling.
  Importing another language of the same Buzz question merges its translation;
  existing text/media is preserved and entirely empty drafts can be filled.
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
3. Choose **Build .edat** or select the game's runtime folder (the folder containing
   `dev_hdd0`) and click **Build and install**. Set the correct title ID if it differs
   from `BCES00647`. The completion dialog shows generated/installed/backup paths.
   Existing packs can also be installed using **… → Install an .edat pack…**.
4. Restart the game and choose the pack in the quiz-pack game mode.

Output is `PACKnnnn/LANGPACKnnnn.edat`. Only the selected language is emitted.
Questions with an entirely empty selected translation are counted and skipped;
partially completed translations stop the build with an error. Other translations
stay in the project. Images retain their proportions in a 16:9 letterboxed canvas.
Supported audio includes WAV/generated TTS, RSF and KiP ATRAC9 WEM; FFmpeg decodes
foreign audio and the native encoder writes ATRAC3. Successfully encoded speech
is cached by content and gain. Imported native SDI/video/music references require
conversion or a replacement image; they are never silently dropped.

In **Advanced settings**, select FFmpeg if it is absent from the application
directory and PATH. Change the category base if the installer reports a collision:
custom category IDs must remain disjoint and fit 500..999. Defaults for packs
101..105 use bases 500, 600, 700, 800, 900. The actual category count also has to fit.
Supported title IDs are BCES00647, BCES00644, BCES00645 and BCUS98209.
The installer writes only the selected language's custom pack (number >=101),
and keeps backups under `runtime/pack-backups/<title>/PACKnnnn/`. Cancelling a
replacement leaves the prior installed file intact. Builds use a document snapshot;
edits made while building belong to the next build. Building does not save the project.

## Project format

Save projects as `name.buzz.json`. The root has `format: "buzz-question-studio"`,
`version: 2`, `pack`, and `questions`. Version 1 projects are read and migrated.
Each question keeps its stable id, four
answers in editor order, explicit zero-based `correct_index`, and unknown source
metadata. Legacy JSON without `correct_index` defaults to answer zero.

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
content language is undoable. Category mappings and the project title are shared.
The interface language does not translate authored questions or change their data.

For Buzz archives, only the selected language is read; import the archive again
with another language to merge it. Native folder codes (POL/ENG/FRE/GER/etc.) are
normalized to editor codes. Missing requested languages fail explicitly. Source
media references remain language-specific, even where previews are unavailable.
The existing buzzpack JSON export emits **only the active language** and requires
its questions to be complete; saving a Studio project preserves all languages.

Media are **linked files**, saved relative to the project's location where
possible. Save As rebases those links. Copy media with the project when moving
between Windows/Linux; arbitrary Windows absolute paths are not portable.
The editor reports missing images/audio. A bundled portable project archive is
a later stage, not implemented by renaming the JSON file.

Recovery/cache/settings use Qt's platform-specific standard paths. No user or
machine-specific paths are compiled into the application. Imported source files
are not modified. Opening a non-Studio source requires Save As to create a project.

## Build on Windows

Install Visual Studio 2022 C++ tools, CMake 3.21+, Ninja, and Qt 6.2+ for MSVC x64
with Qt Declarative/Quick Controls, Qt Tools/Linguist, and Qt SVG. Qt 6.8.3 was tested.
From PowerShell:

```powershell
.\scripts\build-question-studio.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64
.\Launch-Question-Studio.cmd
```

If using the repo's private CMake/Ninja toolenv, set `BUZZ_TOOL_ENV` to its root.
The script builds, runs tests, installs into `dist-studio/`, and uses windeployqt
to collect Qt's runtime dependencies. Running the resulting application needs
neither Python, PowerShell nor a development Qt installation.
For images or audio conversion, install FFmpeg, select its executable in the build
dialog, or include an actual standalone executable using the build helper's
`-FFmpeg C:\path\to\ffmpeg.exe` option. Do not pass a package-manager shim when
preparing a portable directory. FFmpeg is a separate licensed dependency.

## Build on Linux

Ubuntu 22.04/24.04:

```sh
sudo apt-get install build-essential cmake ninja-build qt6-base-dev \
  qt6-declarative-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libqt6svg6-dev \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-dialogs qml6-module-qt-labs-platform \
  qml6-module-qtquick-templates qml6-module-qtqml-workerscript ffmpeg
cmake -S studio -B build-studio/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-studio/linux --parallel
ctest --test-dir build-studio/linux --output-on-failure
./build-studio/linux/BuzzQuestionStudio
cmake --install build-studio/linux --prefix "$HOME/.local"
```

The installed Linux build uses system Qt libraries/QML modules. It is not a
self-contained AppImage. X11 and Wayland use Qt's platform plugins. Automated
Linux verification uses Ubuntu 22.04 under WSL with Qt 6.2.4 and the offscreen
software renderer, plus a screenshot run through X11/Xvfb; this does not
establish hardware/Wayland compositor coverage.

Local TTS on Linux additionally needs `sudo apt-get install espeak-ng`.
On Windows, the voice list contains installed desktop SAPI voices (not all
OneCore/Narrator voices are necessarily exposed through SAPI). Choose a Polish
voice for Polish questions. Generating WAV neither calls a cloud service nor
requires an API key. Changing a question later does not regenerate its speech;
use **Generate speech** again. Undo detaches a generated WAV but keeps the saved file.

`--demo` opens authored sample questions. `--open <file>` opens a library.
`--inspect <file>` runs a headless importer and reports counts/languages/errors;
add `--language en` to select a source language.
`--smoke` loads the QML interface and rejects QML warnings.
`--demo --screenshot <png>` captures the editor; add `--preview` for the preview.
`--demo --tts --screenshot <png>` captures the local voice-generation dialog.
`--ui-language en` sets a session-only interface override without changing the
saved preference. `--show-languages` opens the content-language dialog for capture.
`--show-build` opens the build dialog for capture.

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

1. Decoded native media previews and portable projects containing their media.
2. Direct KiP extracted-folder/PKG import and additional Buzz game adapters.
3. Additional TTS adapters (Piper and HTTP services), internal audio playback,
   batch generation and a generated-audio cache. `src/tts.h` is the provider
   boundary; the first local Windows/Linux adapters already produce WAV.
4. Larger-library tooling, category authoring, additional interface translations,
   release packaging and broader Linux desktop coverage.

Preserve the v7 pack rules: global category ids 1..999, disjoint installed-pack
ids, pack-specific internal category names, native nine-argument AddGame calls,
round registration for subcategories, and 16:9 image metadata/letterboxing.
Keep byte-exact reference fixtures and game smoke checks when changing the binary
writers. The compiled builder shares the v7 registration/progression rules.

## Verification

The same five CTest suites run on both platforms: project format/native fixtures,
controller/import/undo/local TTS, native pack builder, actual QML editing/dialogs
and QML smoke. Builder tests compare every byte of a complete authored pack and
non-silent ATRAC3 output against frozen Python reference fixtures. FFmpeg tests
decode encoded audio (SNR >15 dB) and picture sheets (portrait proportions).
Installation tests cover backups, category collisions, invalid archives, cancellation
and symlinks escaping the selected runtime. QML tests click the build action and
verify the resulting selected-language EDAT. Controller tests exercise asynchronous
build/install/backup and cancellation.
Language tests cover KiP variant retention, native language discovery/selection,
missing-language source filtering and authored drafts, per-language answers/audio/media rebasing, v1 migration,
active-language export, identity merging and undo. QML tests switch PL/EN live,
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
retail games are included. Example questions and the SVG icon are authored here.
Optional FFmpeg is executed as a separate program and retains its own license;
the applicable license depends on the selected build/configuration. The local
Windows development package uses a GPL-enabled FFmpeg build. Preserve its notices
and corresponding distribution materials when distributing that executable.

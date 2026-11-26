BuzzSetup for macOS on Apple Silicon (development preview)

Extract BuzzPrepare-macos-arm64.zip into a folder you can write to. Keep
BuzzSetup.app, BuzzQuestionStudio.app and build-files together. Open
BuzzSetup.app and select your own PS3 disc image or game folder and firmware.
Select Prepare game, then Play Buzz! when preparation completes. The prepared
game and its runtime data are stored in build-files/dist.

Before preparing the game, install Xcode Command Line Tools and the native
Apple Silicon Homebrew dependencies from Terminal:

  xcode-select --install
  brew install python sdl2-compat glslang spirv-cross

The preview uses the Metal renderer, with the reference SPU paths. The app
bundles are development builds with ad hoc signatures and no notarization.
Automatic updates are disabled in this preview.

The licenses folder includes Qt and OpenSSL notices. The matching Qt Base
source archive accompanies the CI artifact. OpenSSL is built from the
Homebrew openssl@3 formula (https://formulae.brew.sh/formula/openssl@3).

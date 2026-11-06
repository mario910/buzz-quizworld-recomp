#!/usr/bin/env bash
# Build, test and stage the Linux preparation application.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build-preparation/linux"
cmake -S "$root/preparation" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel "${1:-4}"
ctest --test-dir "$build" --output-on-failure
cmake --install "$build" --prefix "$root/dist-preparation/linux"

#!/usr/bin/env bash
# Developer helper: build and test the native SDL3/ImGui launcher on Linux.
# Needs CMake 3.31+, Ninja, a C++17 compiler and X11/Wayland development headers.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cmake -S "$root/launcher/native" -B "$root/build-launcher" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$root/build-launcher" --parallel "${1:-4}"
ctest --test-dir "$root/build-launcher" --output-on-failure --timeout 60
echo "Native launcher ready: build-launcher/BuzzLauncher (or ./Launch-Buzz.sh)"

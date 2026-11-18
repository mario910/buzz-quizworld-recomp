#!/usr/bin/env bash
# Create or repair the Ubuntu 24.04 build container BuzzSetup uses on Linux
# hosts without a usable compiler toolchain (SteamOS, Bazzite, Fedora Atomic
# and other image-based systems). Requires distrobox with podman or docker.
#
# Only the container is changed: the host system and its package manager are
# left alone. The prepared game is built inside the container in your home
# folder and runs directly on the host with the host's Vulkan driver.
#
# usage: linux-build-container.sh [container-name]
set -euo pipefail
name="${1:-buzz-prepare}"
image="docker.io/library/ubuntu:24.04"
packages=(build-essential clang cmake ninja-build patch git python3-venv python3-pip pkg-config
          libsdl2-dev libvulkan-dev libssl-dev)

if ! command -v distrobox >/dev/null 2>&1; then
    echo "distrobox is not installed. Install distrobox and podman (or docker) and run this again." >&2
    exit 1
fi
if ! distrobox list --no-color 2>/dev/null | awk -F'|' 'NR>1{gsub(/ /,"",$2); print $2}' | grep -qx "$name"; then
    echo "Creating build container $name from $image (first run downloads about 100 MB)..."
    distrobox create --yes --name "$name" --image "$image"
fi
echo "Installing build packages inside $name..."
distrobox enter "$name" -- sudo env DEBIAN_FRONTEND=noninteractive apt-get update -q
distrobox enter "$name" -- sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends "${packages[@]}"
distrobox enter "$name" -- sh -c 'clang --version | head -1 && pkg-config --modversion sdl2 && python3 --version'
echo "Build container $name is ready."

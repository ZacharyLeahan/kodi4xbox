#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
export NXDK_DIR="${NXDK_DIR:-$root/.local/nxdk}"
if [[ ! -d "$NXDK_DIR" ]]; then
    python3 "$root/scripts/bootstrap-nxdk.py" --directory "$NXDK_DIR"
elif [[ -f "$NXDK_DIR/.kodi-toolchain.json" ]]; then
    python3 "$root/scripts/bootstrap-nxdk.py" --directory "$NXDK_DIR"
fi
# Homebrew LLVM supplies the PE/COFF toolchain; Apple's clang is insufficient.
if [[ -d /opt/homebrew/opt/llvm/bin ]]; then
    export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
fi
export PATH="$NXDK_DIR/bin:$PATH"
jobs="${JOBS:-4}"
build_dir="${KODI_BUILD_DIR:-$root/build}"
make -C "$NXDK_DIR" NXDK_ONLY=y -j"$jobs"
make -C "$NXDK_DIR" tools -j"$jobs"
cmake -S "$root" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel "$jobs" "$@"

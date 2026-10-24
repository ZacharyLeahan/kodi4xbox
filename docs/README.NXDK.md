# Original Xbox Build Guide

Kodi stays on the **21.3 Omega** baseline. The toolchain uses current upstream
NXDK pinned to `119ab2f7b04a92a8ac4f4f35e61e0901224656a1` (2026-09-30), plus
recorded exception/RTTI and C/C++ library compatibility patches. See
[toolchain provenance and runtime limitations](../tools/nxdk/README.md).
Unpatched upstream NXDK disables C++ exceptions and cannot build this port.

Track platform limitations, local workarounds and possible upstream contributions
in the [NXDK improvement ledger](NXDK-IMPROVEMENT-LEDGER.md). Keep app development
on the recorded toolchain; update the ledger as diagnoses and tests change.

The current hardware target is a **128 MiB upgraded Xbox**. Packaging clears
CXBE's default 64 MiB memory-limit flag, and Kodi budgets against installed RAM.
SD output is the tested default while the 720p rendering issue is investigated.
See the [hardware test notes](XBOX-STARTUP-TEST.md) for current runtime results.

For an explicit dashboard return path, configure
`-DKODI_XBOX_DASHBOARD_PATH='E:\Dashboard\default.xbe'` (the verified path on
this rig). An empty value requests the system dashboard through NXDK.
Exit performs Kodi cleanup and environment teardown before launching it.
`Q:\exit.log` records the final teardown stages and selected target; the file
is cleared on startup so an older successful exit cannot mask a new failure.

The earlier normal UI build identified in the hardware notes passed SD navigation
and Exit using the explicit dashboard path, with diagnostics disabled. Current
local build directories may contain newer experiments; compare the recorded XBE
hash before reusing a bundle. The empty/system dashboard path, playback and HD
rendering remain unverified.

Experimental media playback is tracked separately in [issue #2](https://github.com/ZacharyLeahan/kodi4xbox/issues/2).
The player is not enabled in this foundation change.

## Prerequisites

Use Linux, macOS, or Linux through WSL, with a path without spaces. Install
Clang/LLVM **20 or newer**, lld, llvm-ar, llvm-ranlib, CMake 3.18+, make, Git,
Python 3, pkg-config, flex and bison. On Apple Silicon macOS, the build helper
selects Homebrew LLVM when installed; Apple's bundled clang is not sufficient.

## Prepare the SDK and build

From the Kodi checkout:

```sh
python3 scripts/bootstrap-nxdk.py
bash scripts/build-nxdk.sh
```

The SDK is isolated in ignored `.local/nxdk`; existing SDKs in other projects
are untouched. Bootstrap pins upstream and its submodules, applies the patches,
and records patch hashes. It refuses to overwrite an unrelated or incomplete
SDK. To retry a failed bootstrap, select a new `--directory` or deliberately
remove only that incomplete SDK.

The build helper builds NXDK libraries (including C++ and SDL2), host tools,
then Kodi. Set `JOBS=8` to change the default parallelism of four. Outputs are
`build/xbmc.exe`, `build/default.xbe`, `build/xiso/`, and `build/Kodi.iso`.
A successful link or XBE conversion is not a hardware/runtime test.

To use another prepared SDK:

```sh
export NXDK_DIR=/absolute/path/to/prepared/nxdk
bash scripts/build-nxdk.sh
```

Do not change SDK paths inside an existing CMake build directory; use a fresh
build directory when switching toolchains, e.g.
`KODI_BUILD_DIR="$PWD/build/new-sdk" bash scripts/build-nxdk.sh`.
For manual configuration after
building the SDK and its tools:

```sh
export NXDK_DIR="$PWD/.local/nxdk"
export PATH="$NXDK_DIR/bin:/opt/homebrew/opt/llvm/bin:$PATH"
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

To select this rig's dashboard in the existing tested build directory:

```sh
cmake -S . -B build/pinned '-DKODI_XBOX_DASHBOARD_PATH=E:\Dashboard\default.xbe'
cmake --build build/pinned --parallel 4
```

CMake checks both exception/RTTI compiler support and runtime symbol linking.
For on-console checks, build and run the separate
[runtime smoke executable](../tools/nxdk/README.md#runtime-scope).
Keep installation in an isolated E: folder until extended partition support
has been verified. Do not replace the main dashboard.

## How to Debug
- First we need to build NXDK libs as Debug. To do that build hello++ example with `DEBUG = y` added to `samples/hello++/Makefile`
- Debug executable will be quite big because of symbols. Before building apply [this](https://github.com/xbmc4xbox/kodi4xbox/blob/master/docs/resources/debug_kodi.patch) patch:
  ```bash
  curl -L https://raw.githubusercontent.com/xbmc4xbox/kodi4xbox/master/docs/resources/debug_kodi.patch | git apply
  ```
- Build Kodi in same way as described above, but make sure to build Debug version. You do that by setting `-DCMAKE_BUILD_TYPE` to `Debug`
- After succesfull build, open Xemu, load ISO from build folder and then close Xemu. After that, start debugging from within Visual Studio Code by pressing `CTRL+SHIFT+D` and then `F5`

## CI artifacts

Pull requests and pushes to `master` build Kodi, the pinned SDK and the separate
exception/RTTI probe. The workflow uploads the ISO, packaged application, runtime
probe and toolchain identity as a GitHub Actions artifact. It uses the repository's
existing token permissions. Automatic nightly release publication is replaced by
these artifacts; releases can be published separately after validation.

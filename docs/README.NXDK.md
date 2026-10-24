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

**Playback is experimental and deferred in [issue #2](https://github.com/ZacharyLeahan/kodi4xbox/issues/2). Pause currently freezes the console.**

## Initial native playback backend (in progress)

`CNXDKPlayer` connects the previously stubbed `PlayFile` and player factory to
local MPEG-1 program-stream video with optional MP2 stereo audio. Decoding runs
on a worker; pbGL uploads RGBA on the GUI thread; SDL feeds the Xbox AC97 output.
Video is limited to 720x576 by the backend, but full SD performance is not yet
validated. Start at 352 pixels wide. File I/O streams through decoder buffers;
the whole movie is not loaded into RAM. The audio queue is capped at one second.
Files must be seekable local drive paths; the player currently rejects files
larger than INT32_MAX. This does not prove all SDK file APIs have that limit.

This is not the missing Kodi FFmpeg VideoPlayer. AVI/MPEG-4/MP3 (including the
user's original Hook file), MP4/H.264, HEVC, MPEG-2/DVD, MKV, network streams,
subtitles, passthrough and seamless playlists remain unsupported. Upstream
pl_mpeg ignores packet PTS, so arbitrary-file A/V synchronization is not proven.
The dependency and MIT license are recorded in `lib/pl_mpeg/`.

For a compatible local copy (keep the original):

```sh
ffmpeg -i input.avi -vf 'scale=352:-2,setsar=1,fps=25' \
  -c:v mpeg1video -b:v 1000k -c:a mp2 -ar 48000 -ac 2 -b:a 192k \
  -f mpeg output.mpg
```

Host decoder checks, with an automatically generated six-second fixture:

```sh
bash tests/playback/run-host.sh
```

These exercise nonzero video/PCM, forward/backward seeking and EOF with address
and undefined-behavior sanitizers. They do not test Xbox rendering or audio DMA.
The RGBA upload correction eliminated the initial GPU texture-format halt.
Using Kodi file callbacks also fixed an early decoder EOF encountered with NXDK
stdio. A converted Hook excerpt now produces moving video and matching stereo
audio on the 128 MiB Xbox (captured audio correlation 0.9993 against the source).
The console subsequently froze during playback-control checks. Pause, seek,
stop, full-clip playback and playback shutdown are **not yet validated**. The
next diagnostic build adds pause/progress checkpoints and the existing
two-minute dashboard-return watchdog. See `XBOX-STARTUP-TEST.md` for evidence;
the playback candidate is not yet an accepted replacement for the prior UI build.

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

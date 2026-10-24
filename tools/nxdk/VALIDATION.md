# NXDK update validation - 2026-10-20

Kodi remains 21.3 Omega-alpha, working tree based on `a3c61bf`.
Upstream NXDK HEAD was checked against GitHub and pinned to
`119ab2f7b04a92a8ac4f4f35e61e0901224656a1`, with the three patches
documented in [README.md](README.md). Upstream recursive submodule pins are
retained, including PDCLib's newer mutex fix.

## Clean build

On Apple Silicon macOS, using Homebrew Clang/LLVM 23.1.2 and CMake 4.4.3:

```sh
python3 scripts/bootstrap-nxdk.py
KODI_BUILD_DIR="$PWD/build/pinned" JOBS=8 bash scripts/build-nxdk.sh
```

The fresh bootstrap, SDK libraries, SDK host tools, Kodi link, XBE conversion,
asset staging and ISO creation all succeeded. Bootstrap reruns also succeeded.
The first clean-build attempt identified two missing new files in the PDCLib
patch export; the corrected patch was verified by a fresh SDK bootstrap and
full build. No compiled SDK libraries were copied from another project.

| Output | SHA-256 |
| --- | --- |
| `build/pinned/default.xbe` | `d8554822a7afb93cfdb9580644dd8d597d88c4cb46c8a545f92f46dc6c2e21ce` |
| `build/pinned/Kodi.iso` | `10c07909b03fe6edd1a283dbd99752331488079c2a5eb1d2c2284816ad72b491` |

The full build log is retained locally at `build/validation/build.log`.
These hashes identify this build, not a claim of deterministic timestamps.

Additional checks passed:

- CMake compiled and linked the exception/RTTI probe against the prepared SDK.
- A separate configuration with `-fno-exceptions -fno-rtti` failed with the
  intended actionable error, before downloading application dependencies.
- Bootstrap refused to overwrite an unrelated existing directory.
- Python compilation, Bash syntax checks, and `git diff --check` passed.

The GitHub Actions workflow now uses the same bootstrap and build helper on
Ubuntu 24.04 with LLVM 20 and also builds the runtime test. Actions execution
has not been run as part of this local validation.

## Physical Xbox runtime test

The test was built with the freshly prepared SDK and `AUTO_RETURN=1`, uploaded
to its own E: directory and read back over dashboard FTP before launch.
KVM observation showed `RESULT: PASS (0 failures)`. After automatic dashboard
return, FTP log readback independently confirmed all 12 checks passed:

- Typed catch across translation units and destructor execution during unwinding.
- Catch-all/rethrow, RTTI downcast/crosscast, failed cast, null cast and typeid.
- Exception handling on a worker thread.
- Floating-point parsing, formatting, and steady-clock elapsed time.

Tested XBE SHA-256:
`b76679fc16c88d7fb755e96517d065613df05da4a64b05a99602443625233ebf`.

Local evidence is under `build/hardware/runtime-20261020T234116Z/`:
`deploy.json`, `result.json`, `toolchain.json`, `runtime-test.log`,
`runtime-screen.jpg`, and `tested-default.xbe`. The executable was reverified
against the deployment hash after the test. Source hashes are in `result.json`.
The main dashboard and game installations were left intact.

This validates the listed runtime paths on the connected console. It does not
establish full exception_ptr/virtual-inheritance support, stock-64-MB behavior,
DLL loading, Kodi startup, media playback, or general application stability.

## Subsequent application hardware checks

The [startup and shutdown test notes](../../docs/XBOX-STARTUP-TEST.md) track
later application builds separately from the runtime executable above. The
128 MiB diagnostic build renders the SD UI, accepts controller navigation, and
completes GUI teardown and dashboard return after the graphics-lifetime fix,
including with the normal profile. The final normal build
`4ca8648293d35104c9d966d2064d80f0fe8ed99e07ae38aa152f8e987292d2e7`
passed SD home/browser rendering, navigation and normal UI Exit with diagnostics
OFF. It launches the verified `E:\Dashboard\default.xbe` after cleanup; the
dashboard reappeared, FTP recovered and executable readback matched. Logs under
`build/hardware/direct-dash-20261021T012409Z/` confirm environment teardown.
The earlier bare CRT quick reboot and explicit BIOS reboot left black screens.
The cause of an earlier browser stall remains unconfirmed. Media playback,
HD output, and long-running stability remain unverified.

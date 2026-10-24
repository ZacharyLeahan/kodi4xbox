# Kodi 21.3 NXDK toolchain

Upstream NXDK is pinned to `119ab2f7b04a92a8ac4f4f35e61e0901224656a1`
(2026-09-30), upstream HEAD checked on 2026-10-20. Its recursive submodule pins
are retained. The bootstrap applies the following source patches; no proprietary
XDK libraries or prebuilt runtime DLLs are used.
See [validation results](VALIDATION.md) for the clean build and physical Xbox
exception/RTTI checks.

The [NXDK improvement ledger](../../docs/NXDK-IMPROVEMENT-LEDGER.md) tracks why
workarounds remain, which issues are confirmed, component ownership and the
tests required before proposing upstream changes or retiring local patches.

| Patch | Origin and purpose |
| --- | --- |
| `runtime.patch` | Changes from the merge base `d0ae96acc926a6c3f6254b289a1a9a3c1db182f1` to [xbmc4xbox/nxdk `6200488`](https://github.com/xbmc4xbox/nxdk/commit/6200488): C++ exception unwinding, RTTI, timezone conversion, float stub removal and zlib source fixes. Excludes fork submodule downgrades and demo programs. |
| `libcxx.patch` | [xbmc4xbox/nxdk-libcxx `87c2941`](https://github.com/xbmc4xbox/nxdk-libcxx/commit/87c2941b84a7a67d32d8d272998e76aa3a862c54): avoid steady-clock arithmetic overflow. |
| `pdclib.patch` | Fork changes through [xbmc4xbox/nxdk-pdclib `06690c0`](https://github.com/xbmc4xbox/nxdk-pdclib/commit/06690c033f4472cad81de9cfd1edde47bae77f11): floating-point parsing/formatting and log2 implementations, applied on top of current upstream PDCLib. Preserves upstream's later mutex and C++ stdint fixes. |

Original source license/copyright notices are retained in patches. Regenerate
patches from the cited source changes and review them when upgrading the pin;
do not follow a moving exception-fork branch during CI builds.

`SDL2` also carries `sdl-audio-shutdown.patch`, brought over from the user’s
Slime Volley NXDK port. Its AC97 wait checks the SDL shutdown flag every 100 ms
without treating a timeout as DMA completion. This keeps a missing completion
interrupt from blocking device closure forever. The bootstrap records its hash.

## Runtime scope

This preserves an experimental exception runtime, not a complete MSVC runtime.
The inherited implementation has incomplete `std::exception_ptr` operations and
RTTI edge cases, including virtual inheritance. A successful build alone does
not validate these features. `tests/nxdk-runtime` checks cross-translation-unit
throw/catch, destructor unwinding, rethrow, basic and multiple-inheritance RTTI,
worker-thread exceptions, float parsing and the steady clock on the Xbox.
Upstream's `LoadLibraryExA` currently returns `ERROR_MOD_NOT_FOUND`; DLL loading
is not established by these checks and is outside this toolchain update.

Build that executable separately, after building the SDK:

```sh
export NXDK_DIR="$PWD/.local/nxdk"
export PATH="$NXDK_DIR/bin:/opt/homebrew/opt/llvm/bin:$PATH"
mkdir -p build/runtime
make -C build/runtime -f "$PWD/tests/nxdk-runtime/Makefile"
```

The output is `build/runtime/bin/default.xbe`. It displays individual results
and a final PASS/FAIL, writes `D:\runtime-test.log` when the launch directory is
writable, and stays on screen. Retain its SHA-256, screen capture and log.
It must be launched separately from Kodi; it is not a host-executable test.
For this user's rig, `AUTO_RETURN=1` enables a return to
`E:\Dashboard\default.xbe` ten seconds after results, plus a 30-second watchdog.
Verify that dashboard path exists before enabling it. The watchdog cannot
guarantee recovery from kernel faults. The default leaves the results on screen.

The user's Neverball and Slime Volley ports establish the FTP/readback/KVM
workflow. Use an isolated E: directory for Kodi candidates (the application's
extended-partition support is not yet verified). Never replace the dashboard
or reuse a game's deployment directory. KVM captures must be tied to the exact
installed XBE; a responding endpoint or cached picture is not a runtime pass.

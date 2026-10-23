# NXDK improvement ledger

Evidence reviewed: 2026-10-21. Target: Kodi 21.3 on a 128 MiB Xbox.

## Publication scope

This documentation records the project intent and findings from local development.
The toolchain patches, runtime probes, player changes and hardware logs described
below are not included in this documentation change. Their paths identify local
evidence to publish alongside the implementation; they are not download links
or claims that the default branch already reproduces these results.
Full playback controls and cleanup remain unaccepted.

## Purpose and working policy

Build a useful application with the available SDK first. Capture the limitations
we encounter so a later SDK effort is driven by working software, measurements
and small reproductions. The present baseline is upstream NXDK
`119ab2f7b04a92a8ac4f4f35e61e0901224656a1` plus local compatibility patches.
It is not unmodified NXDK. The exception overlay derives from `xbmc4xbox/nxdk` revision `6200488`; the libc++ and PDCLib
patches derive from `xbmc4xbox/nxdk-libcxx` revision
`87c2941b84a7a67d32d8d272998e76aa3a862c54` and `xbmc4xbox/nxdk-pdclib` revision
`06690c033f4472cad81de9cfd1edde47bae77f11`, respectively. Local
`tools/nxdk/README.md` records the complete patch provenance.

- Keep ordinary application development on the pinned, reproducible toolchain.
  Carry narrowly scoped fixes when needed to unblock it; record each patch and
  its removal condition rather than silently expanding the SDK fork.
- Update this ledger when work reveals an SDK limitation, introduces a platform
  workaround, or produces evidence that changes an existing diagnosis.
- Separate observed behavior from suspected causes. An application hang alone
  does not establish an NXDK bug. Identify the owning component before proposing
  an upstream change; pbGL, SDL, PDCLib and Kodi have distinct ownership.
- Prefer application-side workarounds that preserve correctness. Record their
  cost and limitations. Do not promise an improvement without measurement.
- Before contributing a fix, reduce it to an independent example where possible,
  record the exact SDK/patch/XBE identity, and demonstrate failure before and
  success after. Include host checks where useful and physical Xbox evidence
  where required. Keep fault recovery bounded, but do not assume a watchdog can
  recover kernel or GPU faults.
- Keep issue/PR links and upstream landing revisions here when they exist.
  No external issue or PR has been created for the entries below by this work.
- After an upstream fix lands, test a candidate SDK separately. Retire the local
  workaround only after its regression test and relevant Kodi behavior pass.

The existing [upstream roadmap](roadmap/README.md) records the port's original
plans. This ledger records evidence from this fork; a roadmap checkbox is not a
hardware acceptance result. Local `build/hardware/` evidence is ignored by Git;
preserve or export relevant logs and exact binaries before cleaning builds.

## Priority and status

**P1:** correctness or reliability needed by the current app. **P2:** broader
compatibility or measurable performance. **P3:** exploratory capability.
Priority is a triage order, not permission to interrupt application work with an
SDK redesign. Promote speculative work only after a useful reproduction or
benchmark establishes its value.

| ID | Priority | Component / likely owner | State | Application benefit |
| --- | --- | --- | --- | --- |
| SDK-001 | P1 | NXDK C++ runtime / libc++ | Compatibility patches carried; partial hardware validation | Build Kodi with required exceptions and RTTI |
| SDK-002 | P1 | PDCLib / libc++ and runtime compatibility | Compatibility patches carried; selected hardware checks pass | Correct float conversion and monotonic timing |
| SDK-003 | P1 | PDCLib stdio | Suspected defect; Kodi workaround tested | Reliable EOF probing, rewind and media seeking |
| SDK-004 | P1 | SDL Xbox audio backend in SDK dependencies | Local patch; fault-path validation pending | Bounded device shutdown when DMA notification is absent |
| SDK-005 | P2 | pbGL / pbkit / Kodi renderer | Missing pbGL API confirmed; benefit unmeasured | Compressed texture uploads and possible HAP playback |
| SDK-006 | P2 | Kodi video setup / pbGL / NXDK video | Unassigned diagnosis | Reliable 720p output |
| SDK-007 | P3 | NXDK loader / Kodi loader integration | Known runtime limitation; implementation deferred | Optional modules and selected binary add-ons |
| SDK-008 | P2 | Kodi file adapter / NXDK file APIs and stdio | Investigation needed; current player imposes a limit | Larger movie files and reliable long-distance seeking |

## Evidence and next tests

### SDK-001 — Exceptions and RTTI

- **Evidence:** local runtime notes (`tools/nxdk/README.md`) and
  local hardware validation (`tools/nxdk/VALIDATION.md`). The original 12-check
  runtime XBE `b76679fc16c88d7fb755e96517d065613df05da4a64b05a99602443625233ebf`
  passed the documented cross-translation-unit throw/catch, unwinding, rethrow,
  basic/multiple-inheritance RTTI and worker-thread cases. This does not prove
  complete exception support. `exception_ptr` and virtual-inheritance cases
  remain incomplete in the inherited runtime.
- **Current approach:** preserve the recorded exception-fork overlay on current
  upstream submodules; avoid depending on unverified runtime operations.
- **Next SDK work:** separate missing operations into small ABI/runtime tests;
  propose maintainable runtime changes with their original provenance.
- **Exit criterion:** required tests pass on physical hardware with an upstream
  implementation; Kodi builds and relevant application paths still work before
  retiring the overlay. Do not substitute a successful link for runtime proof.

### SDK-002 — Numeric conversion and steady-clock correctness

- **Evidence:** `pdclib.patch`, `libcxx.patch` and relevant parts of
  `runtime.patch`, described in local `tools/nxdk/README.md`.
  The original hardware probe passed its selected float and elapsed-time checks.
- **Current approach:** carry the pinned compatibility patches. Selected passing
  cases do not establish correctness across all inputs or clock ranges.
- **Next SDK work:** reduce patches into focused tests for parsing, formatting,
  arithmetic boundaries and clock overflow; establish which fixes remain needed
  against a candidate upstream revision before submitting or removing them.
- **Exit criterion:** focused tests and the existing runtime probe pass without
  the corresponding local patches. Preserve unrelated upstream library fixes.

### SDK-003 — Repeated EOF reads followed by seek

- **Evidence:** `_PDCLIB_prepread.c` treats `_PDCLIB_EOFFLAG` as an error and sets
  `_PDCLIB_ERRORFLAG`. Playback with decoder stdio duration probing ended after
  one video frame. Kodi CFile read/seek callbacks plus an explicit decoder rewind
  allowed moving video and verified movie audio. Logs and exact candidate hashes
  are in local playback test notes (`docs/XBOX-STARTUP-TEST.md`).
- **Confidence:** source-level concern plus application evidence. The causal
  link has not been independently reproduced on the Xbox; the later playback
  control freeze is a separate unresolved observation.
- **Current approach:** bounded CFile callbacks in `CNXDKPlayer`. This avoids the
  decoder's stdio path; it does not repair stdio for other libraries.
- **Next reproduction:** create a tiny file, read to EOF, read again, inspect
  `feof`/`ferror`, seek to the beginning, and read known bytes. Compare against a
  host reference and repeat on the pinned unmodified and patched PDCLib builds.
- **Exit criterion:** correct stream state and bytes across EOF/seek cycles,
  plus decoder duration/rewind/seek regression tests. Report to PDCLib/NXDK only
  with the independent reproduction and correct ownership established.

### SDK-004 — Audio close must not wait forever for DMA

- **Evidence:** local `tools/nxdk/patches/sdl-audio-shutdown.patch`,
  transferred from the user's Slime Volley port. The original WaitDevice used
  an indefinite semaphore wait. The patch checks shutdown every 100 ms while
  continuing to wait for a genuine DMA completion during normal playback.
- **Current approach:** apply and hash the patch during SDK bootstrap. Movie
  audio has been observed, but missing-interrupt shutdown and repeated playback
  closure have not been accepted on this Kodi build.
- **Next reproduction:** standalone SDL open/play/pause/resume/close loops, then
  a controlled missing-notification case. Record close latency, buffer ownership
  and callback lifetime; a timeout must not permit reuse of an active DMA buffer.
- **Exit criterion:** bounded clean closure in the injected failure and normal
  cases, with repeated Kodi stop/replay/exit passes and no audio regression.

### SDK-005 — GPU-native compressed textures

- **Evidence:** the pinned pbGL `glext.h` comments out compressed-texture upload
  entry points. NXDK pbkit register definitions include DXT texture formats.
  This establishes an API gap, not a functioning HAP player or an HD guarantee.
- **Current approach:** the player uploads RGBA frames. Keep this working path
  while investigating a separate compressed-texture example.
- **Next reproduction:** render known DXT1 blocks through pbkit or an isolated
  pbGL implementation. Verify texture dimensions/padding, block layout, color,
  GPU completion and repeated buffer reuse. Then measure disk throughput,
  upload time, peak memory and missed deadlines for a short texture-video clip.
- **Exit criterion:** an owned, tested renderer API and measured improvement at
  the target resolution. HAP demuxing, scheduling and file segmentation remain
  application work. GPU format support alone does not prove sustained playback.

### SDK-006 — 720p output

- **Evidence:** prior SD rendering passes and black 720p output are recorded in
  local hardware notes (`docs/XBOX-STARTUP-TEST.md`). The responsible layer is unconfirmed.
- **Current approach:** use tested SD output. Do not make HD playback acceptance
  claims from a decoder benchmark or an SD display of an HD source.
- **Next reproduction:** minimal mode-selection and test-pattern program using
  the same console/cable/display; then add pbGL and finally Kodi configuration.
- **Exit criterion:** correct output and repeated mode initialization/teardown
  on this hardware, with the failing layer identified before assigning SDK work.

### SDK-007 — Dynamic loading

- **Evidence:** local runtime notes (`tools/nxdk/README.md`) record upstream
  `LoadLibraryExA` returning `ERROR_MOD_NOT_FOUND`; the runtime probe does not
  establish DLL loading. Kodi's loader code existing in the tree is not proof
  of end-to-end support.
- **Current approach:** statically link the required, selected components.
- **Next work:** first identify a concrete module whose optional loading would
  materially benefit the app. Define ABI, imports, initialization, TLS, exception
  interactions and lifetime requirements before attempting a loader.
- **Exit criterion:** a minimal module loads, calls, unloads and reloads safely;
  the chosen real component works and measured memory savings justify complexity.

### SDK-008 — Large files and offsets

- **Evidence:** `CNXDKPlayer::OpenFile` currently rejects files above `INT32_MAX`.
  This guard does not prove every underlying NXDK file API has the same limit.
  The current truncated Hook sample does not validate seeks across 2 GiB.
- **Current approach:** short local test clips. Do not label a library/API-wide
  limitation as confirmed from the player guard alone.
- **Next reproduction:** read known markers around 2 GiB using kernel/Win32 file
  APIs, CFile and stdio separately; verify return values, offsets and exact data.
  Account for FATX's independent per-file limits when designing the fixture.
- **Exit criterion:** supported ranges documented and tested at each layer;
  remove only justified guards. Logical movie segmentation belongs in Kodi/the
  companion application and cannot be solved by an SDK offset change alone.

## Application and integration work: do not misfile as NXDK bugs

- The corrected `ConditionVariable::notify` lock handling is in Kodi's
  `xbmc/threads/Condition.h`. Its host regression is useful evidence, but the
  unsuccessful later console threading probe is not a passing SDK test.
- The UI shutdown texture-lifetime correction was in Kodi's teardown order.
  The accepted SD UI build returned through the explicit dashboard path; the
  current playback freeze/cleanup remains unaccepted. Neither observation proves
  a kernel reboot defect.
- SQLite 3.51.1 is already bundled. Game metadata, filter queries, indexes,
  optional FTS5 and restoring diagnostic SQL features are application/build work.
  Its custom VFS lacks normal WAL shared-memory methods; investigate that adapter
  before requesting new SDK facilities. Basic filtering does not require WAL.
- FFmpeg selection, codec integration, frame/packet pools and HAP support are
  application/dependency work until a specific missing SDK primitive is isolated.
- 128 MiB is the present target. Stock 64 MiB support is deferred by the user;
  do not turn memory optimization ideas into a new acceptance requirement.

## Entry template

Record: stable ID; date; priority; owning component; observed limitation;
confidence/status; exact revision and local patches; smallest reproduction;
evidence location; application workaround and cost; proposed improvement;
before/after acceptance test; upstream issue/PR and landing revision; and the
condition for removing the local workaround. Use "unmeasured" or "unverified"
instead of filling gaps with expected outcomes.

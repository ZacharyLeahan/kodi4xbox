# Bounded Xbox startup test

The active target is the owner's **128 MiB Xbox**. The build clears CXBE's
default limit-to-64-MiB flag before staging the XBE and ISO, reports installed
RAM to Kodi, and no longer reserves upper RAM. Stock 64 MiB optimization and
verification are deferred. The older memory experiments below are retained
as historical evidence only.

The ordinary Kodi build is unchanged by the startup probe unless
`KODI_XBOX_STARTUP_DIAGNOSTICS=ON` is selected. This option uses a profile and
cache under the installed application's `diagnostics/` folder, records startup
stages and kernel memory statistics in `diagnostics/startup.log`, and requests
normal Kodi shutdown after 100 seconds. The observer attempts return to
`E:\Dashboard\default.xbe` on completed teardown or after 120 seconds. Verify that dashboard
path before using this option on another console. The observer thread cannot
guarantee recovery from a kernel fault.

By default, the diagnostic profile does not read or modify `E:\UDATA\Kodi` or the shared
Z: cache. Set `KODI_XBOX_DIAGNOSTIC_NORMAL_PROFILE=ON` together with diagnostics
to reproduce issues using the normal `E:\UDATA\Kodi` profile and Z: cache.
This opt-in mode reads and writes the normal profile, just like the ordinary
build; collect its `kodi.log` from `E:\UDATA\Kodi`, rather than the isolated
profile folder. Startup and cleanup checkpoints still go under `diagnostics/`.
With the default isolation, on-console Kodi logs are under `diagnostics/profile/`. The telemetry
thread adds overhead, so its memory samples are a diagnostic baseline rather
than a production benchmark.

The current probe records the XBE memory-limit flag, startup memory samples and
synchronous cleanup checkpoints in `diagnostics/shutdown.log`.

## Build

Build the SDK first using the [NXDK guide](README.NXDK.md), then:

```sh
export NXDK_DIR="$PWD/.local/nxdk"
export PATH="$NXDK_DIR/bin:/opt/homebrew/opt/llvm/bin:$PATH"
cmake -S . -B build/diagnostic \
  -DCMAKE_TOOLCHAIN_FILE="$NXDK_DIR/share/toolchain-nxdk.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DKODI_XBOX_STARTUP_DIAGNOSTICS=ON
cmake --build build/diagnostic --parallel 4
```

## Install and observe

```sh
python3 scripts/xbox.py status
python3 scripts/xbox.py deploy
python3 scripts/xbox.py launch
# Observe startup and test navigation through the existing KVM.
# Wait for the diagnostic timeout to restore dashboard FTP, then:
python3 scripts/xbox.py logs
```

The default deployment is the complete `build/diagnostic/xiso` bundle, into
`/E/Kodi-NXDK-test`. The helper accepts only isolated `/E/Kodi-NXDK-...` targets.
It verifies every remote file by SHA-256, resumes partial installs, adapts to
the server's simultaneous-user limit, and records a completed manifest before
allowing launch. Existing logs and the prior executable are downloaded before
an update. It never changes the dashboard's executable.

Local evidence is retained in timestamped `build/hardware/kodi-*` directories.
Launch records link to the deployment's exact XBE and full file hashes. Log
collection does not itself prove a successful boot: inspect the stage log and
actual UI response. Capture a fresh stream frame before and after input; the
KVM still-image endpoint can retain a previous frame during mode transitions.

## Baseline checks

1. Reach splash, then home screen, without fatal log entries.
2. Check text, artwork, colors and screen boundaries at the selected video mode.
3. Use D-pad navigation, select, and back; retain before/after images and logs.
4. Compare memory during startup and repeated navigation on the 128 MiB target.
5. Retrieve logs after automatic dashboard return. Identify the earliest failing
   stage before changing a later subsystem.

## Hardware findings, 2026-10-20

### Shutdown fix verified

The Xbox renderer released pbGL in `DestroyRenderSystem()`, before Kodi
destroyed its remaining GUI texture cache. `CGUITextureManager::Cleanup()`
still calls `glDeleteTextures()`, while pbGL had already freed its texture
table. The context is now released by the Xbox window system's
`DestroyWindowSystem()`, after GUI destruction.

Diagnostic XBE `43f7cf750c2cef61a3ecb8dabb68564a571fd7d6e1e8884842955bef155e4036`
passed a hardware test that opened the video browser, returned home, and used
Power Options > Exit. The logs under
`build/hardware/kodi-20261021T004702529626Z/` record GUI destruction, pbGL
shutdown, cleanup completion and final teardown. Final teardown completed at
73753 ms, before the 100-second stop request or 120-second watchdog. The
dashboard reappeared and FTP logs were retrieved without a manual reboot.
`result.json` summarizes the result; `kodi-shutdown-fix-exit-result.jpg` in the
parent directory captures the returned dashboard.

The diagnostic observer now finishes logging before CRT teardown. Checkpoint
writes explicitly seek to end because this NXDK build did not append using
`FILE_APPEND_DATA` alone.

### Current 128 MiB build

Normal XBE `4ca8648293d35104c9d966d2064d80f0fe8ed99e07ae38aa152f8e987292d2e7`
and ISO `aedf1471033b8e14eae609a406856f8d6fc00d4629c4e221db1524e80eddae13`
passed startup, video-browser navigation, and Power Options > Exit with both
diagnostic options OFF. The dashboard reappeared and FTP recovered without a
manual reboot. The installed XBE was re-read after the test and matched its
deployment hash. The SD UI displayed 60 fps, 89864 KiB free at home and
84908 KiB in the video browser, out of 131072 KiB total.

Deployment and launch records are under `build/hardware/kodi-20261021T012225682077Z/`
and `kodi-20261021T012241865789Z/`. The latter retains the exact executable and
dashboard configuration. `build/hardware/direct-dash-20261021T012409Z/` contains
the recovered logs and result. Both `kodi.log` and `exit.log` confirm application
cleanup and environment teardown completed before the dashboard launch request;
`direct-dash-exit-result.jpg` in the parent folder shows the returned dashboard.

This build uses `XLaunchXBE()` with the verified `E:\Dashboard\default.xbe`,
matching the successful diagnostic launch. The target is configurable with
`KODI_XBOX_DASHBOARD_PATH`; an empty value requests the system dashboard, which
has not been tested on this rig. `Q:\exit.log` records the final teardown stages
without relying on Kodi's logger. It is cleared at startup and is also collected
by `scripts/xbox.py logs`. The installed normal build has no automatic stop timer.

This is a short hardware smoke test. The cause of the earlier browser stall
is still unconfirmed; media playback, HD output and long-running stability
remain open. Stock 64 MiB support remains deferred.

### Earlier final-return failures

The BIOS-reboot candidate was XBE
`77aa2f8557d87a8e6b7ffa79454629961a27522059cd32345d403f16e440ae5c`,
ISO `12b833026fcd89ed90ac3ae22832f22d99554edfcf5faeb7dd04abd28b671373`.
It explicitly called `XReboot()` after application/environment cleanup instead
of returning through CRT exit's bare quick-reboot routine. It compiled with
diagnostics OFF, passed home and video-browser rendering, but Exit again left
the console black with FTP unavailable. It did not solve the final-return issue.
Its verified launch and exact executable are retained under
`build/hardware/kodi-20261021T011739669278Z/`.

The preceding normal XBE
`3a4804d1ad6c6479c7d746c2f02f429bc760849213c06d59238aae647465d85e`
passed two video-browser visits and a Files-view round trip with diagnostics
OFF, showing about 83 MiB free at 60 fps. Its exact executable and launch
record are under `build/hardware/kodi-20261021T011032508881Z/`. Exit then left
a black display and FTP unavailable. A controller reset chord also did not
recover it. The final normal log, recovered under
`build/hardware/normal-exit-recovered-20261021T011708Z/`, ends with
`Exiting the application...` after the timer and announcement threads terminate.
This places the failure after application cleanup, during environment/CRT
teardown or the final dashboard return.

The normal-profile diagnostic XBE
`8268050ac8c6755b0196a0f2e0525ed7083a933e3f001ea69abca6ea48d13995`
passed browser navigation and UI Exit, including GUI destruction, pbGL shutdown,
and final teardown at 72030 ms. Dashboard FTP recovered without manual reboot
and before the watchdog. Logs and exact executable are under
`build/hardware/normal-probe-20261021T011004Z/`; minimum sampled running free
memory was 84456 KiB. This verifies cleanup with the normal profile, but uses
the diagnostic observer's explicit dashboard launch after cleanup.

### Earlier normal-profile browser stall

Normal XBE `3413472249c0a50ef8b5551e5e41d311b0e1ef0bd2738dcd7acba396471512dc`
and ISO `cb37ffc5f9d1b247db4e9aa7e2249670ebfd1490c5b9affeb1d8ec4bac62ee39`
include the shutdown fix, have diagnostics OFF, and clear the 64 MiB limit.
The installed XBE was read-back verified under
`build/hardware/kodi-20261021T004719847905Z/` and launched under
`kodi-20261021T004843093971Z/`. It rendered the home screen at 60 fps with
89736 KiB free of 131072 KiB, using `E:\UDATA\Kodi` as its normal profile.
Opening the video browser then stalled on the background, and subsequent
Back and power-menu input did not restore the UI, so this run did not reach Exit.
Screenshots are `kodi-normal-home-ready.jpg` and `kodi-normal-files-ready.jpg`
under `build/hardware/`.

The recovered normal log is under `build/hardware/normal-profile-20261021T010354Z/`.
It ends after MyVideoNav begins loading and a background recently-added music
query starts. It does not establish the blocked thread or prove a database fault.
The subsequent probe added window-loading checkpoints and retained the normal profile.

A separate host regression confirmed that `ConditionVariable::notify()` tried
to lock its already-held `unique_lock`, throwing a deadlock error. It now
unlocks before waiting for the waiter's acknowledgement, matching `notifyAll()`.
The real header passes the host test in `tests/threads/host`; this is not yet
proof of the browser stall's cause. An expanded standalone Xbox test
(`2657dfd789ff1b635a617f097a90c77a43b1e90d519623549d06b01bcadc61be`)
produced only its title, an empty log, and failed to recover. Its source and
executable are retained in `build/hardware/condition-before-source/`. The
original 12-check exception/runtime test source was restored unchanged; the
expanded probe provides no new exception or threading pass evidence.

### Earlier 128 MiB run before the shutdown fix

Diagnostic XBE `d3222a9cb8d3e4844b8c6aa9a1a622385ba312967b5707d03ca5987a737007ec`
has initialization flags `0x1` (64 MiB restriction cleared). Deployment was
read-back verified under `build/hardware/kodi-20261021T002840319304Z/`, and
launch recorded under `kodi-20261021T002907564461Z/`. The live
`build/hardware/kodi-128-running.jpg` capture shows Estuary at 60 fps with
89668 KiB free out of 131072 KiB. The shutdown attempt stalled. Logs recovered
under `build/hardware/kodi-20261021T004400964890Z/` narrowed the failure to
GUI destruction after service teardown, leading to the fix documented above.

The corresponding normal build compiled successfully with diagnostics OFF:
XBE `e53970283183e66bdf4f7af521fa91aeb41f9a53590d00b2bb04ef83a2e012d7`,
ISO `a9cc528844508de6acdec32ddfb1f393e94efa4f0de34b03942fe5515f31bf17`.
The normal XBE has not been hardware-tested yet. Header-patching checks verified
that only the intended flag changes, repeated application is harmless, and
malformed input is rejected without modification.

### Earlier 64 MiB experiments (deferred)

The connected console has 128 MiB installed. The XBE's limit-to-64-MiB flag
alone did not prevent an explicit upper-memory allocation. Do not interpret
the flag or the GUI's 65536 KiB total as proof of a stock-console test.

The SD default now renders Estuary at 720x480. Before that change, 720p
produced a black display despite continued rendering. Avoiding a redundant
video-mode switch alone did not fix it; the exact 720p failure remains open.
The earlier 64 MiB budgeting clamp has since been removed for the 128 MiB target.

The diagnostic XBE `4117ce607542681d61e76029ae4e704947f2997d4c4829901cf6320ac351d387`
rendered home and accepted remote Down, Right, Select and Back after the KVM
USB cable was reconnected. Captures under `build/hardware/` are
`kodi-constrained-down.jpg`, `kodi-constrained-select.jpg`, and
`kodi-constrained-back.jpg`. The UI showed about 17.5–23 MiB free during this
short navigation test. Log readback under
`build/hardware/kodi-20261021T002137533581Z/` confirms Down, Right, Select and
Back, with a minimum sampled running free memory of 17892 KiB. The last sample
at 95694 ms recorded 20708 KiB free and 4841 rendered frames. The first line
records `upper_reserved_kib=4` and `upper_64mib_page_available=0`: this is not
the intended full upper-memory reservation and does not verify stock 64 MiB
behavior. `result.json` summarizes the evidence. Media playback and
long-running stability remain untested.

Both the earlier direct dashboard return and this run's graceful shutdown
attempt stalled before dashboard FTP became available. Logs retrieved after
physical reboot show `Application stopped`, `Unloaded skin`, then `unload
sections` and a timer thread terminating. In the tested source, `unload
sections` follows renderer and window destruction, so the remaining stall is
later in cleanup. The observer log also stops before 100 seconds and contains
no dashboard-return marker. Later cleanup checkpoints identified the texture
destruction ordering issue documented above.

The ordinary build (probe disabled) also compiled successfully after these
changes: `build/pinned/default.xbe` SHA-256
`69884773cc62b2cae492624a82c4bf4064daa7f68c31cad1ef135fdecce51085`;
`build/pinned/Kodi.iso` SHA-256
`66e555997520fbc0fa30f7f0b5a17e4c2a1b980913f3d9a9ef573e9b3716b38a`.
This ordinary build has not yet been launched on the console.

## Playback bring-up, 2026-10-21 UTC (pending)

Added a local MPEG-1/MP2 backend, the missing application PlayFile connection,
player factory construction, fullscreen window, pause/seek/stop, volume/mute,
and end-of-file cleanup. SDL shutdown uses the Slime Volley wait-loop patch.
No change to the exception/RTTI patches or the 128 MiB target.

Found the user's Hook AVI on F:; ffprobe identifies MPEG-4 Part 2 720x300 and
48 kHz stereo MP3 despite the HEVC filename. Its original file was not modified.
A 70-second 352x144 MPEG-1/MP2 excerpt is at
`E:\Kodi-NXDK-test\media\Hook-test.mpg`, accessible through the added
“NXDK playback tests” video source. Original profile sources were saved locally
at `build/media/sources-before.xml`. No movie media is committed to the repo.

First executable `6035cfb3a720ce04d43604f10b6a84af42fe99115d8d6d47e326b19f81f7498d`
booted and reached the file browser, then halted with GPU error at texture-format
register 0x1b04 when playing. `playback-hook-second.jpg` captures the halt;
`playback-hook-first-audio.json` reports silence, not successful audio playback.
pbGL's `extfmt_bytespp` explicitly rejects GL_BGRA. The correction uses RGBA,
checks upload errors before drawing, and waits for GPU completion before
updating the texture in place.

Corrected candidate `1bc98ca29f717c5d36dfcb587e2d30fa7c8779a4a65b612467a4982944918d81`
builds, and host ASan/UBSan checks pass for decode, nonzero PCM, forward/backward
seek and EOF on both a generated fixture and the Hook excerpt. **Hardware
playback is not yet validated.** The console needs a physical reboot; FTP was
unreachable and remote power is disabled. Candidate, toolchain identity and
resume checklist: `build/hardware/playback-rgba-candidate/`.

The prior normal UI/navigation/dashboard-exit acceptance remains the separate
`4ca8648293d35104c9d966d2064d80f0fe8ed99e07ae38aa152f8e987292d2e7`
build recorded above. Do not count that result as playback validation.

### Playback follow-up: video/audio confirmed, control stall unresolved

The RGBA candidate above booted without the GPU halt but reached EOF after just
one video frame and 3456 audio samples/channel. Both attempts and a clean UI
exit are recorded in `build/hardware/playback-rgba-logs/`. The transferred
7,266,304-byte clip matched its local SHA-256
`7e06208f573565b69b77afb9fee7fcfdb42a72483deabfc303a3674e335fca8e`.
NXDK stdio marks repeated EOF reads as errors; duration probing followed by
seeking is a likely trigger. This SDK behavior has not been separately reproduced
by an on-console regression test. The player now streams through Kodi CFile
callbacks and explicitly rewinds the decoder after probing duration.

Candidate `d88d4acc1c818477c705527c2c9c08c194af1a329d874348d83f5739f900cf68`
was deployed with verified readback (`build/hardware/kodi-20261021T102730602077Z/`).
`playback-fileio-hook1.jpg` and `playback-fileio-hook2.jpg` show different movie
frames. `playback-fileio-hook.wav` contains 5.965 seconds of captured stereo
audio; a two-second normalized correlation against the converted source was
0.999275 at source offset 10.837 seconds. This confirms movie audio, not measured
lip synchronization. The overlay showed roughly 82–89 MiB free in these frames.

The console subsequently froze on the auditorium frame during control checks.
Pause/resume, seek, OSD and Stop did not produce further screen changes. The KVM
USB probe reported bus and mount connected; FTP was unavailable. The capture
named `playback-fileio-pause.wav` contains low-level audio and does not establish
a successful pause. Do not count any playback controls or EOF as hardware passes.
A physical reboot is needed to recover this run's normal profile kodi.log.

The next diagnostic candidate includes five-second decoder/queue checkpoints,
logs around pause state changes and SDL pause calls, and a correction to update
cached playback time immediately after seeking while paused. It uses the normal
profile and the existing 120-second dashboard-return observer. That observer may
recover a thread stall but cannot guarantee recovery from a kernel/GPU halt.

### Pause reproduction, 2026-10-24 UTC

Dashboard FTP recovered at the existing address. Prior logs were saved before
deployment under `build/hardware/kodi-20261024T125953923009Z/`, with SHA-256
digests. The normal profile log confirms receipt of Pause and ends at
`Window Init (DialogSeekBar.xml)`. This narrows the trigger but does not identify
the blocked operation or establish an SDL, GUI or SDK defect.

Diagnostic XBE
`64416d49c5a7a46e20e77c433836ba571f52a4ef17f4815f84a540dd4eb78525`
was deployed with readback verification under
`build/hardware/kodi-20261024T130012906828Z/` and launched under
`build/hardware/kodi-20261024T130052472245Z/`. Browser navigation and movie
launch worked, but Pause again left the movie frame frozen. FTP remained
unavailable after the observer's 120-second return window. A physical reboot
was requested to retrieve this run's pause and shutdown checkpoints; no control,
EOF or playback-exit acceptance is established by this run.

The KVM still-image endpoint initially returned the previous auditorium frame
despite working dashboard FTP. A fresh MJPEG stream confirmed the dashboard;
subsequent observations used stream captures. Evidence includes
`build/hardware/control-probe-movie.jpg`, `control-probe-pause-attempt.jpg` and
`control-probe-recovery.jpg`. Do not use a cached still as proof of console state.

The diagnostic logs were subsequently recovered to
`build/hardware/kodi-20261024T135437148535Z/`. They show `pause command complete`
and seek-bar resource allocation completing, then stop before any
`applying audio pause` checkpoint. The observer's last sample has 83520 KiB
free. This narrows the next investigation to seek-bar initialization; it does
not establish an SDL pause defect or RAM exhaustion.

A focused diagnostic adds seek-bar initialization phases and per-control
visibility/animation checkpoints. XBE
`8d9da6fc90f394c5a48645ff0b4768defcd2147af2929c9cfde769a3483d287e`
built successfully, passed executable readback verification in
`build/hardware/kodi-20261024T135527654122Z/`, and launched in
`build/hardware/kodi-20261024T135549862784Z/`. Pause again stalled before the
seek bar became visible; dashboard FTP remained unavailable beyond the recovery
deadline. Its detailed trace is pending another physical reboot. The saved
candidate and status are in `build/hardware/seekbar-init-candidate/`.

## Deferred handoff

Playback work was paused at the owner's request on 2026-10-24.
[Issue #2](https://github.com/ZacharyLeahan/kodi4xbox/issues/2) records the resume
checklist. The detailed seek-bar trace remains on the console pending recovery.
The source is preserved in the playback draft PR; no further hardware testing
is scheduled as part of this handoff.

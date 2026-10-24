# pl_mpeg

Vendored unchanged from https://github.com/phoboslab/pl_mpeg at commit
`c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`. MIT license is included in `pl_mpeg.h`.

Used by the native NXDK player for MPEG-1 video and MP2 stereo in local MPEG
program streams. This is an initial backend, not Kodi's FFmpeg VideoPlayer.
The upstream decoder ignores packet PTS and does not support MPEG-2, MPEG-4,
H.264, HEVC, AVI, MP4, or MKV. It is not a hardened parser for untrusted media.

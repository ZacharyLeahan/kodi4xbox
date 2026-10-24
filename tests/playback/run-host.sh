#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/build/playback-tests"
mkdir -p "$out"
ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i testsrc2=size=352x240:rate=25 \
  -f lavfi -i sine=frequency=440:sample_rate=48000 \
  -t 6 -c:v mpeg1video -b:v 900k -c:a mp2 -ac 2 -f mpeg "$out/fixture.mpg"
"${CC:-clang}" -O1 -g -fsanitize=address,undefined \
  "$root/tests/playback/decoder.c" -lm -o "$out/decoder-test"
"$out/decoder-test" "$out/fixture.mpg"
"$out/decoder-test" "$out/fixture.mpg" callbacks

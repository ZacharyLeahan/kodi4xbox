// SPDX-License-Identifier: GPL-2.0-or-later
// Run with a locally generated MPEG-1/MP2 fixture; no copyrighted media is committed.
#include <stddef.h>
#include <stdio.h>
#define PL_MPEG_IMPLEMENTATION
#include "../../lib/pl_mpeg/pl_mpeg.h"
#include <assert.h>
#include <math.h>
#include <string.h>

// Match the player's streaming adapter contract, including real EOF and seeks
// after duration probing. This tests pl_mpeg's callback API, not Xbox CFile.
static void load_input(plm_buffer_t *buffer, void *context) {
  unsigned char bytes[64 * 1024];
  FILE *file = context;
  size_t count = fread(bytes, 1, sizeof(bytes), file);
  assert(!ferror(file));
  if (count) plm_buffer_write(buffer, bytes, count);
  else buffer->has_ended = TRUE;
}
static void seek_input(plm_buffer_t *buffer, size_t offset, void *context) {
  (void)buffer;
  assert(fseek(context, offset, SEEK_SET) == 0);
}
static size_t tell_input(plm_buffer_t *buffer, void *context) {
  (void)buffer;
  long offset = ftell(context);
  assert(offset >= 0);
  return offset;
}
static unsigned frames, blocks;
static double energy;
static void video(plm_t *p, plm_frame_t *f, void *u) {
  (void)p; (void)u; assert(f->width > 0 && f->height > 0); ++frames;
}
static void audio(plm_t *p, plm_samples_t *s, void *u) {
  (void)p; (void)u; ++blocks;
  for (unsigned i=0;i<s->count*2;i++) { assert(isfinite(s->interleaved[i])); energy += s->interleaved[i]*s->interleaved[i]; }
}
int main(int argc, char **argv) {
  assert(argc == 2 || (argc == 3 && strcmp(argv[2], "callbacks") == 0));
  FILE *input = NULL;
  plm_t *p;
  if (argc == 3) {
    input = fopen(argv[1], "rb"); assert(input);
    assert(fseek(input, 0, SEEK_END) == 0);
    long size = ftell(input); assert(size > 0);
    rewind(input);
    plm_buffer_t *buffer = plm_buffer_create_with_callbacks(
        load_input, seek_input, tell_input, size, input);
    assert(buffer);
    p = plm_create_with_buffer(buffer, TRUE);
  } else p = plm_create_with_filename(argv[1]);
  assert(p && plm_has_headers(p));
  double duration=plm_get_duration(p); assert(duration>3 && duration<120);
  plm_rewind(p);
  plm_set_video_decode_callback(p,video,NULL); plm_set_audio_decode_callback(p,audio,NULL);
  plm_set_audio_lead_time(p, 0.20);
  plm_decode(p, 0.001);
  assert(frames == 1 && blocks >= 8);
  for(unsigned i=0;i<250 && !plm_has_ended(p);i++) plm_decode(p,0.01);
  assert(frames>10 && blocks>10 && energy>0);
  assert(plm_seek(p,duration/2,TRUE)); assert(fabs(plm_get_time(p)-duration/2)<0.2);
  assert(plm_seek(p,0,TRUE));
  frames=blocks=0;
  for(unsigned i=0;i<(duration+2)*100 && !plm_has_ended(p);i++) plm_decode(p,0.01);
  assert(plm_has_ended(p)); assert(frames > duration*plm_get_framerate(p)-3);
  printf("PASS (%s): duration/rewind, prefill, nonzero PCM, seek forward/back, EOF (%u frames, %u audio blocks)\n",
         input ? "callbacks" : "stdio", frames, blocks);
  plm_destroy(p);
  if (input) fclose(input);
}

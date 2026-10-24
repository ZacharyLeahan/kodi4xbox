/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PL_MPEG_IMPLEMENTATION
#include "NXDKPlayer.h"

#include "FileItem.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationVolumeHandling.h"
#include "ServiceBroker.h"
#include "cores/DataCacheCore.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/GLUtils.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "windowing/WinSystem.h"
#include "windowing/GraphicContext.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>

CNXDKPlayer::CNXDKPlayer(IPlayerCallback& callback) : IPlayer(callback), CThread("NXDKPlayer") {}
CNXDKPlayer::~CNXDKPlayer() { CloseFile(); }

bool CNXDKPlayer::OpenFile(const CFileItem& file, const CPlayerOptions& options)
{
  CloseFile(true);
  auto path = CSpecialProtocol::TranslatePath(file.GetDynPath());
  StringUtils::Replace(path, '/', '\\');
  if (path.size() < 3 || path[1] != ':' || path[2] != '\\')
  {
    CLog::Log(LOGERROR, "NXDKPlayer: only local drive paths are supported: {}", path);
    return false;
  }
  // Reject other containers before passing them to a decoder that deliberately
  // offers little error reporting. This backend accepts MPEG-1 program streams.
  if (!m_file.Open(path)) return false;
  const auto size = m_file.GetLength();
  unsigned char header[5]{};
  const bool mpeg1 = m_file.Read(header, sizeof(header)) == sizeof(header) &&
      header[0] == 0 && header[1] == 0 && header[2] == 1 && header[3] == 0xba &&
      (header[4] & 0xf0) == 0x20;
  if (!mpeg1 || size <= 0 || size > INT32_MAX || m_file.Seek(0) != 0)
  {
    m_file.Close();
    CLog::Log(LOGERROR, "NXDKPlayer: unsupported container or file size: {}", path);
    return false;
  }
  m_bytesRead = m_fileSeeks = 0;
  m_failed = false;
  auto* input = plm_buffer_create_with_callbacks(ReadInput, SeekInput, TellInput, size, this);
  m_decoder = plm_create_with_buffer(input, TRUE);
  if (!m_decoder || !plm_has_headers(m_decoder) || plm_get_num_video_streams(m_decoder) != 1)
  {
    CLog::Log(LOGERROR, "NXDKPlayer: expected an MPEG-1 program stream: {}", path);
    CloseFile(true);
    return false;
  }
  m_width = plm_get_width(m_decoder);
  m_height = plm_get_height(m_decoder);
  const double fps = plm_get_framerate(m_decoder);
  if (m_width <= 0 || m_width > 720 || m_height <= 0 || m_height > 576 || fps <= 0 || fps > 60)
  {
    CLog::Log(LOGERROR, "NXDKPlayer: unsupported dimensions/rate {}x{} {} fps", m_width, m_height, fps);
    CloseFile(true);
    return false;
  }
  m_duration = plm_get_duration(m_decoder);
  // Duration probing seeks to EOF. Start decoding from a clean demux/codec state.
  plm_rewind(m_decoder);
  CLog::Log(LOGINFO, "NXDKPlayer: file {} bytes, duration probe read {} bytes / {} seeks",
            size, m_bytesRead, m_fileSeeks);
  if (!std::isfinite(m_duration) || m_duration <= 0)
  {
    CLog::Log(LOGERROR, "NXDKPlayer: missing/invalid duration");
    CloseFile(true);
    return false;
  }
  m_textureWidth = m_textureHeight = 1;
  while (m_textureWidth < m_width) m_textureWidth *= 2;
  while (m_textureHeight < m_height) m_textureHeight *= 2;
  m_pixels.assign(m_textureWidth * m_textureHeight * 4, 255);
  m_audioSamples = m_decodedFrames = 0;
  m_paused = false;
  const auto volume = CServiceBroker::GetAppComponents().GetComponent<CApplicationVolumeHandling>();
  m_volume = volume->GetVolumeRatio();
  m_muted = volume->IsMuted();
  m_seek = -1;
  m_start = std::clamp(options.starttime, 0.0, std::max(0.0, m_duration - 0.1));
  if (options.startpercent > 0)
    m_start = std::clamp(options.startpercent / 100.0 * m_duration, 0.0, m_duration - 0.1);

  if (plm_get_num_audio_streams(m_decoder) && !options.videoOnly)
  {
    m_rate = plm_get_samplerate(m_decoder);
    if (m_rate <= 0 || SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
      CLog::Log(LOGERROR, "NXDKPlayer: audio initialization failed: {}", SDL_GetError());
      CloseFile(true);
      return false;
    }
    m_audioInitialized = true;
    SDL_AudioSpec desired{};
    desired.freq = m_rate;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 1024;
    // SDL converts to the Xbox AC97 driver's 48 kHz signed-16 stereo format.
    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (!m_audioDevice)
    {
      CLog::Log(LOGERROR, "NXDKPlayer: audio device failed: {}", SDL_GetError());
      CloseFile(true);
      return false;
    }
    plm_set_audio_decode_callback(m_decoder, AudioFrame, this);
    plm_set_audio_lead_time(m_decoder, 0.20);
  }
  else
    plm_set_audio_enabled(m_decoder, FALSE);
  plm_set_video_decode_callback(m_decoder, VideoFrame, this);
  plm_set_loop(m_decoder, FALSE);
  auto& cache = CDataCacheCore::GetInstance();
  cache.SetVideoDecoderName("pl_mpeg (MPEG-1)", false);
  cache.SetVideoDimensions(m_width, m_height);
  cache.SetVideoFps(fps);
  cache.SetVideoDAR(GetRenderAspectRatio());
  cache.SetVideoPixelFormat("yuv420p");
  cache.SetAudioDecoderName(HasAudio() ? "pl_mpeg (MP2) / SDL AC97" : "");
  cache.SetAudioSampleRate(m_rate);
  cache.SetAudioChannels(HasAudio() ? "stereo" : "");
  cache.SetSpeed(1, 1);
  cache.SetGuiRender(true);
  cache.SetVideoRender(false);
  cache.SetPlayTimes(0, m_start * 1000, 0, m_duration * 1000);
  CLog::Log(LOGINFO, "NXDKPlayer: open {} ({}x{}, {} fps, {} Hz, {} seconds)",
            path, m_width, m_height, fps, m_rate, m_duration);
  CLog::Log(LOGDEBUG, "NXDKPlayer: starting decoder worker");
  m_playing = true;
  m_callback.OnPlayBackStarted(file);
  m_callback.OnAVStarted(file);
  try
  {
    Create();
  }
  catch (const std::exception& error)
  {
    CLog::Log(LOGERROR, "NXDKPlayer: cannot start decoder: {}", error.what());
    CloseFile(true);
    return false;
  }
  return true;
}

bool CNXDKPlayer::CloseFile(bool reopen)
{
  const bool wasPlaying = m_playing.exchange(false);
  StopThread();
  if (m_audioDevice)
  {
    SDL_CloseAudioDevice(m_audioDevice);
    m_audioDevice = 0;
  }
  if (m_audioInitialized)
  {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_audioInitialized = false;
  }
  if (m_decoder)
  {
    plm_destroy(m_decoder);
    m_decoder = nullptr;
    CLog::Log(LOGINFO, "NXDKPlayer: closed, decoded {} video frames, {} audio samples/channel",
              m_decodedFrames, m_audioSamples);
  }
  m_file.Close();
  FlushRenderer();
  m_width = m_height = m_rate = 0;
  m_duration = 0;
  m_pixels.clear();
  m_frameReady = m_frameDirty = false;
  if (wasPlaying && !reopen) m_callback.OnPlayBackStopped();
  return true;
}

void CNXDKPlayer::ReadInput(plm_buffer_t* buffer, void* context)
{
  auto& self = *static_cast<CNXDKPlayer*>(context);
  const auto count = self.m_file.Read(self.m_readBuffer.data(), self.m_readBuffer.size());
  if (count > 0)
  {
    plm_buffer_write(buffer, self.m_readBuffer.data(), count);
    self.m_bytesRead += count;
  }
  else
  {
    // File-mode buffers need a real EOF marker; do not invent bytes on errors.
    buffer->has_ended = TRUE;
    if (count < 0)
    {
      CLog::Log(LOGERROR, "NXDKPlayer: read failed at {}", self.m_file.GetPosition());
      self.m_failed = true;
    }
  }
}

void CNXDKPlayer::SeekInput(plm_buffer_t*, size_t offset, void* context)
{
  auto& self = *static_cast<CNXDKPlayer*>(context);
  ++self.m_fileSeeks;
  if (self.m_file.Seek(offset) != static_cast<int64_t>(offset))
  {
    CLog::Log(LOGERROR, "NXDKPlayer: file seek to {} failed", offset);
    self.m_failed = true;
  }
}

size_t CNXDKPlayer::TellInput(plm_buffer_t*, void* context)
{
  return static_cast<CNXDKPlayer*>(context)->m_file.GetPosition();
}

void CNXDKPlayer::VideoFrame(plm_t*, plm_frame_t* frame, void* context)
{
  auto& self = *static_cast<CNXDKPlayer*>(context);
  if (frame->width != static_cast<unsigned>(self.m_width) ||
      frame->height != static_cast<unsigned>(self.m_height))
  {
    self.m_failed = true;
    return;
  }
  std::lock_guard<std::mutex> lock(self.m_frameLock);
  plm_frame_to_rgba(frame, self.m_pixels.data(), self.m_textureWidth * 4);
  self.m_frameReady = self.m_frameDirty = true;
  ++self.m_decodedFrames;
}

void CNXDKPlayer::AudioFrame(plm_t*, plm_samples_t* samples, void* context)
{
  auto& self = *static_cast<CNXDKPlayer*>(context);
  const float gain = self.m_muted ? 0.0f : self.m_volume.load();
  for (unsigned i = 0; i < samples->count * 2; ++i)
    samples->interleaved[i] *= gain;
  // Never allow a stalled audio device to grow an unbounded queue.
  if (SDL_GetQueuedAudioSize(self.m_audioDevice) > static_cast<unsigned>(self.m_rate * 8) ||
      SDL_QueueAudio(self.m_audioDevice, samples->interleaved, samples->count * 8) != 0)
    self.m_failed = true;
  else
    self.m_audioSamples += samples->count;
}

void CNXDKPlayer::Process()
{
  double base = 0;
  if (m_start > 0 && plm_seek(m_decoder, m_start, FALSE))
    base = plm_get_time(m_decoder);
  else
    plm_decode(m_decoder, 0.001); // first frame + bounded audio prefill
  CLog::Log(LOGINFO, "NXDKPlayer: prefilled {} frames and {} audio samples/channel", m_decodedFrames, m_audioSamples);
  if (m_audioDevice) SDL_PauseAudioDevice(m_audioDevice, 0);
  auto last = std::chrono::steady_clock::now();
  auto lastProgress = last;
  double previous = -1;
  auto lastReport = last;
  bool audioPaused = false;
  while (!m_bStop && !m_failed)
  {
    const bool paused = m_paused;
    if (m_audioDevice && paused != audioPaused)
    {
      CLog::Log(LOGINFO, "NXDKPlayer: applying audio pause={}", paused);
      SDL_PauseAudioDevice(m_audioDevice, paused);
      CLog::Log(LOGINFO, "NXDKPlayer: audio pause applied");
      audioPaused = paused;
    }
    const auto seek = m_seek.exchange(-1);
    if (seek >= 0)
    {
      if (m_audioDevice)
      {
        SDL_PauseAudioDevice(m_audioDevice, 1);
        SDL_ClearQueuedAudio(m_audioDevice);
      }
      m_audioSamples = 0;
      const double before = plm_get_time(m_decoder);
      if (!plm_seek(m_decoder, seek / 1000.0, FALSE))
      {
        CLog::Log(LOGERROR, "NXDKPlayer: seek failed");
        m_failed = true;
        break;
      }
      base = plm_get_time(m_decoder);
      CDataCacheCore::GetInstance().SetPlayTimes(0, base * 1000, 0, m_duration * 1000);
      if (m_audioDevice) SDL_PauseAudioDevice(m_audioDevice, paused);
      CDataCacheCore::GetInstance().SeekFinished((base - before) * 1000);
      m_callback.OnPlayBackSeek(base * 1000, (base - before) * 1000);
      CLog::Log(LOGINFO, "NXDKPlayer: seek to {} seconds", base);
      lastProgress = std::chrono::steady_clock::now();
    }
    auto now = std::chrono::steady_clock::now();
    if (now - lastReport >= std::chrono::seconds(5))
    {
      CLog::Log(LOGINFO, "NXDKPlayer: progress {} ms, {} frames, {} audio samples, queue={} bytes, paused={}",
                int64_t(plm_get_time(m_decoder) * 1000), m_decodedFrames, m_audioSamples,
                m_audioDevice ? SDL_GetQueuedAudioSize(m_audioDevice) : 0, paused);
      lastReport = now;
    }
    const double elapsed = std::chrono::duration<double>(now - last).count();
    last = now;
    if (!paused)
    {
      double target = plm_get_time(m_decoder) + std::min(elapsed, 0.10);
      if (m_audioDevice)
      {
        const auto queued = SDL_GetQueuedAudioSize(m_audioDevice) / 8;
        if (queued) target = base + (m_audioSamples - queued) / static_cast<double>(m_rate);
      }
      const double tick = std::clamp(target - plm_get_time(m_decoder), 0.0, 0.10);
      plm_decode(m_decoder, tick);
      if (plm_has_ended(m_decoder)) break;
      const double current = plm_get_time(m_decoder);
      CDataCacheCore::GetInstance().SetPlayTimes(0, current * 1000, 0, m_duration * 1000);
      if (current > previous) lastProgress = now;
      previous = current;
      if (now - lastProgress > std::chrono::seconds(5) || current > m_duration + 2)
      {
        CLog::Log(LOGERROR, "NXDKPlayer: playback stopped making progress");
        m_failed = true;
      }
    }
    else
      lastProgress = now;
    Sleep(std::chrono::milliseconds(5));
  }
  // Drain the short queued tail at EOF, but stop promptly on user cancellation.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!m_bStop && !m_failed && m_audioDevice && SDL_GetQueuedAudioSize(m_audioDevice) &&
         std::chrono::steady_clock::now() < deadline)
    Sleep(std::chrono::milliseconds(5));
  if (!m_bStop)
  {
    m_playing = false;
    CLog::Log(LOGINFO, "NXDKPlayer: {} after {} video frames", m_failed ? "error" : "end of file", m_decodedFrames);
    if (m_failed) m_callback.OnPlayBackError();
    else m_callback.OnPlayBackEnded();
  }
}

void CNXDKPlayer::Pause() { SetSpeed(m_paused ? 1 : 0); }
void CNXDKPlayer::SetSpeed(float speed)
{
  if (speed != 0 && speed != 1) return;
  const bool paused = speed == 0;
  if (m_paused.exchange(paused) == paused) return;
  CLog::Log(LOGINFO, "NXDKPlayer: pause command {} before state update", paused);
  CDataCacheCore::GetInstance().SetSpeed(1, speed);
  CLog::Log(LOGINFO, "NXDKPlayer: {}", paused ? "paused" : "resumed");
  if (paused) m_callback.OnPlayBackPaused(); else m_callback.OnPlayBackResumed();
  CLog::Log(LOGINFO, "NXDKPlayer: pause command complete");
}
void CNXDKPlayer::Seek(bool forward, bool large, bool)
{
  SeekTime(CDataCacheCore::GetInstance().GetPlayTime() + (forward ? 1 : -1) * (large ? 60000 : 10000));
}
void CNXDKPlayer::SeekTime(int64_t milliseconds)
{
  m_seek = std::clamp<int64_t>(milliseconds, 0, std::max<int64_t>(0, m_duration * 1000 - 100));
}
void CNXDKPlayer::SeekPercentage(float percent) { SeekTime(m_duration * percent * 10); }
void CNXDKPlayer::SetVolume(float volume) { m_volume = std::clamp(volume, 0.0f, 1.0f); }
float CNXDKPlayer::GetRenderAspectRatio() const { return m_height ? float(m_width) / m_height : 1.0f; }

void CNXDKPlayer::FlushRenderer()
{
  if (!m_texture) return;
  auto& gfx = CServiceBroker::GetWinSystem()->GetGfxContext();
  std::unique_lock<CCriticalSection> lock(gfx);
  glDeleteTextures(1, &m_texture);
  m_texture = 0;

}

void CNXDKPlayer::Render(bool clear, uint32_t alpha, bool gui)
{
  if (!gui || !m_playing) return;
  std::lock_guard<std::mutex> lock(m_frameLock);
  if (!m_frameReady) return;
  auto& gfx = CServiceBroker::GetWinSystem()->GetGfxContext();
  if (clear) gfx.Clear();
  // pbGL supports RGBA input; BGRA is rejected before allocating a texture.
  // Never submit an incomplete texture to NV2A.
  while (glGetError() != GL_NO_ERROR) {}
  glActiveTexture(GL_TEXTURE0);
  const bool allocate = m_texture == 0;
  if (allocate) glGenTextures(1, &m_texture);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  glEnable(GL_TEXTURE_2D);
  if (allocate)
  {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_textureWidth, m_textureHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
  }
  else if (m_frameDirty)
  {
    // pbGL uploads in place: the previous draw must finish before we rewrite it.
    glFinish();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_textureWidth, m_textureHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
  }
  const auto error = glGetError();
  if (error != GL_NO_ERROR)
  {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    CLog::Log(LOGERROR, "NXDKPlayer: texture upload failed (GL error {})", error);
    m_failed = true;
    return;
  }
  m_frameDirty = false;
  CRect rect = gfx.GetViewWindow();
  float width = rect.Width(), height = width / GetRenderAspectRatio();
  if (height > rect.Height()) { height = rect.Height(); width = height * GetRenderAspectRatio(); }
  const float x = rect.x1 + (rect.Width() - width) / 2;
  const float y = rect.y1 + (rect.Height() - height) / 2;
  const float u = float(m_width) / m_textureWidth, v = float(m_height) / m_textureHeight;
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4ub(255, 255, 255, alpha);
  glBegin(GL_QUADS);
  glTexCoord2f(0, 0); glVertex3f(x, y, 0);
  glTexCoord2f(u, 0); glVertex3f(x + width, y, 0);
  glTexCoord2f(u, v); glVertex3f(x + width, y + height, 0);
  glTexCoord2f(0, v); glVertex3f(x, y + height, 0);
  glEnd();
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

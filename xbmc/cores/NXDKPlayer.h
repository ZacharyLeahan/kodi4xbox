/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "IPlayer.h"
#include "threads/Thread.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <array>
#include "filesystem/File.h"

#include <cstdio>
#include "../../lib/pl_mpeg/pl_mpeg.h"

// First native playback backend: local MPEG-1 program streams with MP2 audio.
// Decoding/file I/O stay off the GUI thread; GL stays on the GUI thread.
class CNXDKPlayer final : public IPlayer, private CThread
{
public:
  explicit CNXDKPlayer(IPlayerCallback& callback);
  ~CNXDKPlayer() override;
  bool OpenFile(const CFileItem& file, const CPlayerOptions& options) override;
  bool CloseFile(bool reopen = false) override;
  bool IsPlaying() const override { return m_playing; }
  bool HasVideo() const override { return m_width != 0; }
  bool HasAudio() const override { return m_audioDevice != 0; }
  void Pause() override;
  void SetSpeed(float speed) override;
  bool CanSeek() const override { return m_duration > 0; }
  void Seek(bool forward, bool large, bool chapter) override;
  void SeekTime(int64_t milliseconds) override;
  void SeekPercentage(float percent) override;
  void SetVolume(float volume) override;
  void SetMute(bool mute) override { m_muted = mute; }
  void Render(bool clear, uint32_t alpha, bool gui) override;
  void FlushRenderer() override;
  bool IsRenderingVideo() const override { return m_playing && m_width != 0; }
  float GetRenderAspectRatio() const override;
  void GetAudioCapabilities(std::vector<int>& caps) const override { caps.clear(); }
  void GetSubtitleCapabilities(std::vector<int>& caps) const override { caps.clear(); }

private:
  void Process() override;
  static void VideoFrame(plm_t*, plm_frame_t* frame, void* context);
  static void AudioFrame(plm_t*, plm_samples_t* samples, void* context);
  static void ReadInput(plm_buffer_t* buffer, void* context);
  static void SeekInput(plm_buffer_t* buffer, size_t offset, void* context);
  static size_t TellInput(plm_buffer_t* buffer, void* context);
  XFILE::CFile m_file;
  std::array<uint8_t, 64 * 1024> m_readBuffer{};
  uint64_t m_bytesRead{0};
  unsigned m_fileSeeks{0};
  plm_t* m_decoder{nullptr};
  unsigned int m_audioDevice{0};
  bool m_audioInitialized{false};
  int m_rate{0}, m_width{0}, m_height{0}, m_textureWidth{0}, m_textureHeight{0};
  double m_duration{0}, m_start{0};
  uint64_t m_audioSamples{0}, m_decodedFrames{0};
  std::atomic<bool> m_playing{false}, m_paused{false}, m_failed{false}, m_muted{false};
  std::atomic<float> m_volume{1.0f};
  std::atomic<int64_t> m_seek{-1};
  std::mutex m_frameLock;
  std::vector<uint8_t> m_pixels;
  bool m_frameReady{false}, m_frameDirty{false};
  unsigned int m_texture{0};
};

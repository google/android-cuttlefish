#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cuttlefish/host/frontend/webrtc/libdevice/audio_sink.h"
#include "cuttlefish/host/frontend/webrtc/audio_settings.h"

namespace cuttlefish {

class AudioMixer {
 public:
  AudioMixer(std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
             const AudioMixerSettings& settings);
  ~AudioMixer();

  void Start();
  void Stop();

  // Called by auido_handler whenever new playback data chunk is given
  // Can be called on different threads
  void OnPlayback(uint32_t stream_id, uint32_t stream_sample_rate,
                    uint8_t stream_channels_count,
                    uint8_t stream_bits_per_channel, const uint8_t* buffer,
                    size_t size);
  void OnStreamStopped(uint32_t stream_id);

 private:
  // The main mixing loop that runs on its own thread.
  void MixerLoop();

  const uint8_t channels_count_;
  const uint32_t sample_rate_;
  const size_t sample_size_bytes_ = sizeof(int16_t);
  const size_t frame_size_bytes_ = sample_size_bytes_ * channels_count_;
  // Frames count for 10ms at mixer's sample rate
  const size_t chunk_frames_count_ = sample_rate_ / 100;

  std::shared_ptr<webrtc_streaming::AudioSink> audio_sink_;

  std::mutex mutex_;
  ////////////////////////////////////////////////////
  ///////////////// Guarded by mutex_ ////////////////
  ////////////////////////////////////////////////////

  // Buffer stores mixed auido data for every active stream. Consumed by MixerLoop
  std::vector<uint8_t> mixed_buffer_;

  // Index of the last frame with available (i.e. not yet played) audio data
  size_t last_active_frame_ = 0;

  // Frame index per stream to put next avaiable data to
  std::unordered_map<uint32_t, size_t> next_frame_;

  ////////////////////////////////////////////////////
  ////////////////////////////////////////////////////

  std::thread mixer_thread_;
  std::atomic<bool> stop_mixer_{false};
  std::condition_variable mixer_cv_;
};

}  // namespace cuttlefish

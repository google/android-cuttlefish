#include "cuttlefish/host/frontend/webrtc/audio_mixer.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include <android-base/logging.h>

namespace cuttlefish {
namespace {

class CvdAudioFrameBuffer : public webrtc_streaming::AudioFrameBuffer {
 public:
  CvdAudioFrameBuffer(const uint8_t* buffer, int bits_per_sample,
                      int sample_rate, int channels, int frames)
      : buffer_(buffer),
        bits_per_sample_(bits_per_sample),
        sample_rate_(sample_rate),
        channels_(channels),
        frames_(frames) {}

  int bits_per_sample() const override { return bits_per_sample_; }

  int sample_rate() const override { return sample_rate_; }

  int channels() const override { return channels_; }

  int frames() const override { return frames_; }

  const uint8_t* data() const override { return buffer_; }

 private:
  const uint8_t* buffer_;
  int bits_per_sample_;
  int sample_rate_;
  int channels_;
  int frames_;
};

inline size_t GetFramesCount(size_t buffer_size_bytes, uint8_t channels_count,
                             uint8_t bits_per_sample) {
  const auto buffer_size_bits = buffer_size_bytes * 8;
  return buffer_size_bits / (channels_count * bits_per_sample);
}

inline size_t GetFrameCountAfterResampling(uint32_t target_rate,
                                           uint32_t source_rate,
                                           size_t source_frames_count) {
  if (target_rate == source_rate) {
    return source_frames_count;
  }
  return source_frames_count * target_rate / source_rate;
}

template <class DST, class SRC>
size_t ConvertAudioStream(void* dst, uint8_t dst_channels, uint32_t dst_rate,
                          const void* src, uint8_t src_channels,
                          uint32_t src_rate, size_t src_frames_count,
                          const std::vector<std::vector<float>>& channel_map) {
  constexpr uint8_t kMaxChannelsCount = 6;

  CHECK(src_channels <= kMaxChannelsCount);
  CHECK(channel_map.size() >= dst_channels);
  CHECK(std::all_of(channel_map.cbegin(), channel_map.cbegin() + dst_channels,
                    [src_channels](const std::vector<float>& v) {
                      return v.size() >= src_channels;
                    }));

  std::array<SRC, kMaxChannelsCount> src_frame_resampled = {};

  const auto factor = static_cast<double>(dst_rate) / src_rate;
  const auto dst_frames_count =
      GetFrameCountAfterResampling(dst_rate, src_rate, src_frames_count);

  auto dst_typed = reinterpret_cast<DST*>(dst);
  auto src_typed = reinterpret_cast<const SRC*>(src);

  for (size_t frame_id = 0; frame_id < dst_frames_count; ++frame_id) {
    const double src_frame_idx_float = frame_id / factor;
    const size_t src_frame_idx_1 = static_cast<size_t>(src_frame_idx_float);
    const size_t src_frame_idx_2 =
        std::min(src_frame_idx_1 + 1, src_frames_count - 1);
    const float fraction = src_frame_idx_float - src_frame_idx_1;

    // Resample a single frame
    for (size_t c = 0; c < src_channels; ++c) {
      const auto sample1 = src_typed[src_frame_idx_1 * src_channels + c];
      const auto sample2 = src_typed[src_frame_idx_2 * src_channels + c];
      src_frame_resampled[c] =
          static_cast<SRC>(sample1 + (sample2 - sample1) * fraction);
    }

    // Mix and apply channel mapping
    const auto dst_frame_begin = frame_id * dst_channels;
    for (auto i = 0; i < dst_channels; ++i) {
      for (auto j = 0; j < src_channels; ++j) {
        const int64_t value =
            static_cast<int64_t>(dst_typed[dst_frame_begin + i]) +
            src_frame_resampled[j] * channel_map[i][j];
        dst_typed[dst_frame_begin + i] = static_cast<DST>(
            std::clamp<int64_t>(value, std::numeric_limits<DST>::min(),
                                std::numeric_limits<DST>::max()));
      }
    }
  }
  return dst_frames_count;
}

using ConvertAudioStreamFn = size_t(void*, uint8_t, uint32_t, const void*,
                                    uint8_t, uint32_t, size_t,
                                    const std::vector<std::vector<float>>&);
/*
 * The kConvertAudioStreamFunctionMap variable is a 2D array of function
 * pointers for converting between different audio sample formats. The rows are
 * indexed by the destination format's size in bytes per sample, while the
 * columns are indexed by the source format's size. This provides an efficient
 * way to look up the correct conversion function based on the source and
 * destination audio formats.
 * 0-byte and 3-bytes samples are not supported.
 * */
constexpr std::array<std::array<ConvertAudioStreamFn*, 5>, 5>
    kConvertAudioStreamFunctionMap = {
        {{nullptr, nullptr, nullptr, nullptr, nullptr},
         {nullptr, ConvertAudioStream<int8_t, int8_t>,
          ConvertAudioStream<int8_t, int16_t>, nullptr,
          ConvertAudioStream<int8_t, int32_t>},
         {nullptr, ConvertAudioStream<int16_t, int8_t>,
          ConvertAudioStream<int16_t, int16_t>, nullptr,
          ConvertAudioStream<int16_t, int32_t>},
         {nullptr, nullptr, nullptr, nullptr, nullptr},
         {nullptr, ConvertAudioStream<int32_t, int8_t>,
          ConvertAudioStream<int32_t, int16_t>, nullptr,
          ConvertAudioStream<int32_t, int32_t>}}};
}  // namespace

AudioMixer::AudioMixer(std::shared_ptr<webrtc_streaming::AudioSink> audio_sink,
                       const AudioMixerSettings& settings)
    : channels_count_(GetChannelsCount(settings.channels_layout)),
      sample_rate_(settings.sample_rate),
      audio_sink_(std::move(audio_sink)),
      mixed_buffer_(chunk_frames_count_ * frame_size_bytes_) {}

AudioMixer::~AudioMixer() { Stop(); }

void AudioMixer::Start() {
  mixer_thread_ = std::thread([this]() { MixerLoop(); });
}

void AudioMixer::Stop() {
  stop_mixer_.store(true);
  mixer_cv_.notify_one();
  if (mixer_thread_.joinable()) {
    mixer_thread_.join();
  }
}

void AudioMixer::OnStreamStopped(uint32_t stream_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  next_frame_.erase(stream_id);
}

void AudioMixer::OnPlayback(uint32_t stream_id, uint32_t stream_sample_rate,
                            uint8_t stream_channels_count,
                            uint8_t stream_bits_per_channel,
                            const uint8_t* buffer, size_t size) {
  static const std::vector<std::vector<float>> kDirectMap{{
      {1, 0, 0, 0, 0, 0},
      {0, 1, 0, 0, 0, 0},
      {0, 0, 1, 0, 0, 0},
      {0, 0, 0, 1, 0, 0},
      {0, 0, 0, 0, 1, 0},
      {0, 0, 0, 0, 0, 1},
  }};

  const auto stream_frames_count =
      GetFramesCount(size, stream_channels_count, stream_bits_per_channel);
  const auto frames_count = GetFrameCountAfterResampling(
      sample_rate_, stream_sample_rate, stream_frames_count);

  std::unique_lock<std::mutex> lock(mutex_);
  const bool need_notify = next_frame_.empty();  // no active streams

  const auto it = next_frame_.find(stream_id);
  // If stream was not active it will start from the next 10ms bucket
  const auto next_frame_id = it == next_frame_.end() ? 0 : it->second;

  // Resize the buffer to fit extra frames_count samples
  const auto buffer_size = mixed_buffer_.size();
  if (buffer_size < (next_frame_id + frames_count) * frame_size_bytes_) {
    mixed_buffer_.resize((next_frame_id + frames_count) * frame_size_bytes_);
  }

  if (next_frame_id + frames_count > last_active_frame_) {
    std::fill(mixed_buffer_.data() + last_active_frame_ * frame_size_bytes_,
              mixed_buffer_.data() +
                  (next_frame_id + frames_count) * frame_size_bytes_,
              0);
  }

  const auto stream_sample_size_bytes = stream_bits_per_channel / 8;
  const auto convert_fn =
      kConvertAudioStreamFunctionMap[sample_size_bytes_]
                                    [stream_sample_size_bytes];
  CHECK(convert_fn) << "Format is not supported";
  const auto filled_frames_count =
      convert_fn(mixed_buffer_.data() + next_frame_id * frame_size_bytes_,
                 channels_count_, sample_rate_, buffer, stream_channels_count,
                 stream_sample_rate, stream_frames_count, kDirectMap);
  CHECK(filled_frames_count <= frames_count);

  next_frame_[stream_id] = next_frame_id + filled_frames_count;
  last_active_frame_ =
      std::max(last_active_frame_, next_frame_id + filled_frames_count);

  if (need_notify) {
    lock.unlock();
    mixer_cv_.notify_one();
  }
}

void AudioMixer::MixerLoop() {
  static constexpr auto kInterval = std::chrono::milliseconds(10);  // milliseconds

  auto next_frame_time = std::chrono::system_clock::now();

  while (!stop_mixer_.load()) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (next_frame_time > std::chrono::system_clock::now() &&
        mixer_cv_.wait_until(lock, next_frame_time) ==
            std::cv_status::no_timeout) {
      continue;
    }

    if (next_frame_.empty() && last_active_frame_ == 0) {
      // No active streams and nothing to play, block until there is
      mixer_cv_.wait(
          lock, [&]() { return !next_frame_.empty() || stop_mixer_.load(); });
      if (stop_mixer_.load()) {
        return;
      }
      next_frame_time = std::chrono::system_clock::now();
    }

    if (last_active_frame_ < chunk_frames_count_) {
      // Fill the rest of 10ms bucket with 0s
      std::fill(mixed_buffer_.data() + last_active_frame_ * frame_size_bytes_,
                mixed_buffer_.data() + chunk_frames_count_ * frame_size_bytes_,
                0);
    }

    const CvdAudioFrameBuffer audio_frame_buffer(
        mixed_buffer_.data(), sample_size_bytes_ * 8, sample_rate_,
        channels_count_, chunk_frames_count_);

    const int64_t timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            next_frame_time.time_since_epoch())
            .count();
    audio_sink_->OnFrame(audio_frame_buffer, timestamp_ms);
    next_frame_time += kInterval;

    // Frame
    for (auto& [id, frame] : next_frame_) {
      if (frame > chunk_frames_count_) {
        frame -= chunk_frames_count_;
      } else {
        frame = 0;
      }
    }
    last_active_frame_ = last_active_frame_ > chunk_frames_count_
                             ? last_active_frame_ - chunk_frames_count_
                             : 0;
    if (last_active_frame_ > 0) {
      std::memmove(
          mixed_buffer_.data(),
          mixed_buffer_.data() + chunk_frames_count_ * frame_size_bytes_,
          last_active_frame_ * frame_size_bytes_);
    }
  }
}

}  // namespace cuttlefish

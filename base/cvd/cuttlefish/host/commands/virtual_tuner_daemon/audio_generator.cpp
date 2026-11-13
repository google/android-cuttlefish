/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"

#include <fcntl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/VirtualTuner.pb.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

constexpr int16_t kNoiseAmplitude = 4000;
constexpr double kToneAmplitude = kNoiseAmplitude;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr size_t kRiffHeaderBytes = 12;
constexpr size_t kChunkHeaderBytes = 8;
constexpr size_t kMinFmtChunkBytes = 16;
constexpr uint16_t kWavFormatPcm = 1;
constexpr uint16_t kBitsPerSample = kBytesPerSample * 8;

// Writes `sample` to every channel of the frame starting at `frame`.
void WriteFrame(std::span<int16_t> samples, size_t frame, int16_t sample) {
  for (size_t channel = 0; channel < kChannels; ++channel) {
    samples[frame + channel] = sample;
  }
}

uint16_t ReadLe16(std::span<const uint8_t> bytes, size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t ReadLe32(std::span<const uint8_t> bytes, size_t offset) {
  return static_cast<uint32_t>(ReadLe16(bytes, offset)) |
         (static_cast<uint32_t>(ReadLe16(bytes, offset + 2)) << 16);
}

std::string_view FourCc(std::span<const uint8_t> bytes, size_t offset) {
  return std::string_view(reinterpret_cast<const char*>(bytes.data() + offset),
                          4);
}

Result<uint16_t> ValidateFmtChunk(std::span<const uint8_t> payload) {
  CF_EXPECTF(payload.size() >= kMinFmtChunkBytes,
             "WAV fmt chunk too small ({} bytes)", payload.size());
  const uint16_t audio_format = ReadLe16(payload, 0);
  const uint16_t num_channels = ReadLe16(payload, 2);
  const uint32_t sample_rate = ReadLe32(payload, 4);
  const uint16_t bits_per_sample = ReadLe16(payload, 14);
  CF_EXPECTF(audio_format == kWavFormatPcm,
             "Unsupported WAV format {} (expected 1 for PCM)", audio_format);
  CF_EXPECTF(num_channels == 1 || num_channels == kChannels,
             "Unsupported WAV channel count {} (expected 1 or {})",
             num_channels, kChannels);
  CF_EXPECTF(sample_rate == kSampleRate,
             "Unsupported WAV sample rate {} Hz (expected {} Hz)", sample_rate,
             kSampleRate);
  CF_EXPECTF(bits_per_sample == kBitsPerSample,
             "Unsupported WAV bit depth {} (expected {})", bits_per_sample,
             kBitsPerSample);
  return num_channels;
}

Result<void> PReadBytes(const Fd& file, uint64_t offset,
                        std::span<uint8_t> out) {
  size_t total = 0;
  while (total < out.size()) {
    const uint64_t read = CF_EXPECT(
        file.PRead(out.data() + total, out.size() - total, offset + total));
    CF_EXPECT(read > 0, "Unexpected EOF while reading WAV header");
    total += read;
  }
  return {};
}

struct WavHeader {
  uint16_t channels = 0;
  size_t data_offset = 0;
  size_t data_size = 0;
  size_t frame_count = 0;
};

using ReadBytesFn = std::function<Result<void>(size_t, std::span<uint8_t>)>;

Result<WavHeader> ParseWavHeader(size_t total_size,
                                 const ReadBytesFn& read_at) {
  std::array<uint8_t, kRiffHeaderBytes> riff_header{};
  CF_EXPECT(total_size >= kRiffHeaderBytes, "Payload is not a RIFF/WAVE file");
  CF_EXPECT(read_at(0, riff_header));
  CF_EXPECT(
      FourCc(riff_header, 0) == "RIFF" && FourCc(riff_header, 8) == "WAVE",
      "Payload is not a RIFF/WAVE file");

  std::optional<uint16_t> channels;
  std::optional<size_t> data_offset;
  std::optional<uint32_t> data_size;
  size_t offset = kRiffHeaderBytes;
  while (offset + kChunkHeaderBytes <= total_size) {
    std::array<uint8_t, kChunkHeaderBytes> chunk_header{};
    CF_EXPECT(read_at(offset, chunk_header));
    const std::string_view chunk_id = FourCc(chunk_header, 0);
    const uint32_t chunk_size = ReadLe32(chunk_header, 4);
    const size_t payload_offset = offset + kChunkHeaderBytes;
    CF_EXPECTF(chunk_size <= total_size - payload_offset,
               "Truncated '{}' chunk in WAV payload", chunk_id);

    if (chunk_id == "fmt ") {
      std::vector<uint8_t> fmt_payload(chunk_size);
      CF_EXPECT(read_at(payload_offset, fmt_payload));
      channels = CF_EXPECT(ValidateFmtChunk(fmt_payload));
    } else if (chunk_id == "data") {
      data_offset = payload_offset;
      data_size = chunk_size;
      break;
    }

    const size_t padded_size =
        static_cast<size_t>(chunk_size) + (chunk_size & 1u);
    if (padded_size > total_size - payload_offset) {
      break;
    }
    offset = payload_offset + padded_size;
  }

  CF_EXPECT(channels.has_value(), "Missing fmt chunk in WAV payload");
  CF_EXPECT(data_offset.has_value() && data_size.has_value(),
            "Missing data chunk in WAV payload");
  const size_t input_frame_bytes = *channels * kBytesPerSample;
  const size_t frame_count = *data_size / input_frame_bytes;
  CF_EXPECT(frame_count > 0, "WAV data chunk has no audio frames");
  return WavHeader{
      .channels = *channels,
      .data_offset = *data_offset,
      .data_size = *data_size,
      .frame_count = frame_count,
  };
}

}  // namespace

double StationToneHz(const TunerStateSnapshot& snapshot) {
  if (!snapshot.station_present) {
    return 0.0;
  }
  const uint32_t khz = snapshot.frequency_hz / 1000;
  if (khz == 0) {
    return 0.0;
  }
  double tone_hz = 0.0;
  switch (snapshot.band) {
    case RadioBand::AM:
      tone_hz = 300.0 + 0.5 * (std::max<uint32_t>(khz, 530) - 530);
      break;
    case RadioBand::FM:
      tone_hz = 1000.0 + (std::max<uint32_t>(khz, 87500) - 87500) / 10.0;
      break;
    case RadioBand::DAB:
      return 5000.0 + (std::max<uint32_t>(khz, 174000) - 174000) / 50.0;
    default:
      return 0.0;
  }
  return tone_hz + 1000.0 * snapshot.hd_subchannel;
}

Result<std::vector<int16_t>> ParseWavBytes(std::span<const uint8_t> wav_bytes) {
  const WavHeader header = CF_EXPECT(ParseWavHeader(
      wav_bytes.size(),
      [wav_bytes](size_t offset, std::span<uint8_t> out) -> Result<void> {
        CF_EXPECT(offset + out.size() <= wav_bytes.size(),
                  "Unexpected EOF while reading WAV header");
        std::copy_n(wav_bytes.begin() + offset, out.size(), out.begin());
        return {};
      }));
  const std::span<const uint8_t> data_chunk =
      wav_bytes.subspan(header.data_offset, header.data_size);
  const size_t input_frame_bytes = header.channels * kBytesPerSample;

  std::vector<int16_t> samples(header.frame_count * kChannels);
  for (size_t frame = 0; frame < header.frame_count; ++frame) {
    const size_t frame_offset = frame * input_frame_bytes;
    for (size_t channel = 0; channel < kChannels; ++channel) {
      const size_t src_channel = (header.channels == 1) ? 0 : channel;
      samples[frame * kChannels + channel] = static_cast<int16_t>(
          ReadLe16(data_chunk, frame_offset + src_channel * kBytesPerSample));
    }
  }
  return samples;
}

Result<void> ValidateWavFile(const std::string& wav_path) {
  Fd file = CF_EXPECTF(Fd::Open(wav_path, O_RDONLY),
                       "Failed to open WAV file '{}'", wav_path);
  const uint64_t file_size = CF_EXPECT(file.SeekEnd(0));
  CF_EXPECT(ParseWavHeader(
      file_size,
      [&file](size_t offset, std::span<uint8_t> out) -> Result<void> {
        return PReadBytes(file, offset, out);
      }));
  return {};
}

Result<std::vector<int16_t>> LoadWavFile(const std::string& wav_path) {
  std::vector<uint8_t> contents =
      CF_EXPECTF(ReadBinaryFileContents(wav_path),
                 "Failed to read WAV file '{}'", wav_path);
  return ParseWavBytes(contents);
}

AudioGenerator::AudioGenerator() : rng_(std::random_device()()) {}

void AudioGenerator::GenerateChunk(std::span<int16_t> samples,
                                   const TunerStateSnapshot& snapshot) {
  if (!snapshot.is_playing) {
    active_wav_path_.clear();
    active_wav_samples_.clear();
    wav_cursor_ = 0;
    std::fill(samples.begin(), samples.end(), 0);
    return;
  }

  if (snapshot.station_wav_path != active_wav_path_) {
    active_wav_path_ = snapshot.station_wav_path;
    active_wav_samples_.clear();
    wav_cursor_ = 0;
    if (!active_wav_path_.empty()) {
      Result<std::vector<int16_t>> loaded = LoadWavFile(active_wav_path_);
      if (loaded.has_value()) {
        active_wav_samples_ = std::move(*loaded);
      } else {
        LOG(WARNING) << "Failed to load WAV file '" << active_wav_path_
                     << "': " << loaded.error().Message();
      }
    }
  }

  if (!active_wav_samples_.empty()) {
    for (int16_t& out_sample : samples) {
      out_sample = active_wav_samples_[wav_cursor_];
      wav_cursor_ = (wav_cursor_ + 1) % active_wav_samples_.size();
    }
    return;
  }

  // Plays StationToneHz() when tuned to a station, or white noise when no
  // station is broadcasting on the current frequency.
  const double tone_hz = StationToneHz(snapshot);
  if (tone_hz <= 0.0) {
    std::uniform_int_distribution<int> noise(-kNoiseAmplitude, kNoiseAmplitude);
    for (size_t frame = 0; frame + kChannels <= samples.size();
         frame += kChannels) {
      WriteFrame(samples, frame, static_cast<int16_t>(noise(rng_)));
    }
    return;
  }

  const double phase_step = kTwoPi * tone_hz / kSampleRate;
  for (size_t frame = 0; frame + kChannels <= samples.size();
       frame += kChannels) {
    const auto tone =
        static_cast<int16_t>(std::lround(kToneAmplitude * std::sin(phase_)));
    WriteFrame(samples, frame, tone);
    phase_ = std::fmod(phase_ + phase_step, kTwoPi);
  }
}

}  // namespace virtualtuner
}  // namespace cuttlefish

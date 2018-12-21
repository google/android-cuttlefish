// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// https://github.com/google/opuscpp

#ifndef OPUSCPP_OPUS_WRAPPER_H_
#define OPUSCPP_OPUS_WRAPPER_H_

#include <memory>
#include <string>
#include <vector>

#include "opus.h"

namespace opus {

std::string ErrorToString(int error);

namespace internal {
// Deleter for OpusEncoders and OpusDecoders
struct OpusDestroyer {
  void operator()(OpusEncoder* encoder) const noexcept;
  void operator()(OpusDecoder* decoder) const noexcept;
};
template <typename T>
using opus_uptr = std::unique_ptr<T, OpusDestroyer>;
}  // namespace internal

class Encoder {
 public:
  // see documentation at:
  // https://mf4.xiph.org/jenkins/view/opus/job/opus/ws/doc/html/group__opus__encoder.html#gaa89264fd93c9da70362a0c9b96b9ca88
  // Fs corresponds to sample_rate
  //
  // If expected_loss_percent is positive, FEC will be enabled
  Encoder(opus_int32 sample_rate, int num_channels, int application,
          int expected_loss_percent = 0);

  // Resets internal state of encoder. This should be called between encoding
  // different streams so that back-to-back decoding and one-at-a-time decoding
  // give the same result. Returns true on success.
  bool ResetState();

  // Sets the desired bitrate. Rates from 500 to 512000 are meaningful as well
  // as the special values OPUS_AUTO and OPUS_BITRATE_MAX. If this method
  // is not called, the default value of OPUS_AUTO is used.
  // Returns true on success.
  bool SetBitrate(int bitrate);

  // Enables or disables variable bitrate in the encoder. By default, variable
  // bitrate is enabled. Returns true on success.
  bool SetVariableBitrate(int vbr);

  // Sets the computational complexity of the encoder, in the range of 0 to 10,
  // inclusive, with 10 being the highest complexity. Returns true on success.
  bool SetComplexity(int complexity);

  // Gets the total samples of delay added by the entire codec. This value
  // is the minimum amount of 'preskip' that has to be specified in an
  // ogg-stream that encapsulates the encoded audio.
  int GetLookahead();

  // Takes audio data and encodes it. Returns a sequence of encoded packets.
  // pcm.size() must be divisible by frame_size * (number of channels);
  // pcm must not contain any incomplete packets.
  // see documentation for pcm and frame_size at:
  // https://mf4.xiph.org/jenkins/view/opus/job/opus/ws/doc/html/group__opus__encoder.html#gad2d6bf6a9ffb6674879d7605ed073e25
  std::vector<std::vector<unsigned char>> Encode(
      const std::vector<opus_int16>& pcm, int frame_size);

  int valid() const { return valid_; }

 private:
  std::vector<unsigned char> EncodeFrame(
      const std::vector<opus_int16>::const_iterator& frame_start,
      int frame_size);

  template <typename... Ts>
  int Ctl(int request, Ts... args) const {
    return opus_encoder_ctl(encoder_.get(), request, args...);
  }

  int num_channels_{};
  bool valid_{};
  internal::opus_uptr<OpusEncoder> encoder_;
};

class Decoder {
 public:
  // see documentation at:
  // https://mf4.xiph.org/jenkins/view/opus/job/opus/ws/doc/html/group__opus__decoder.html#ga753f6fe0b699c81cfd47d70c8e15a0bd
  // Fs corresponds to sample_rate
  Decoder(opus_uint32 sample_rate, int num_channels);

  // Takes a sequence of encoded packets and decodes them. Returns the decoded
  // audio.
  // see documentation at:
  // https://mf4.xiph.org/jenkins/view/opus/job/opus/ws/doc/html/group__opus__decoder.html#ga7d1111f64c36027ddcb81799df9b3fc9
  std::vector<opus_int16> Decode(
      const std::vector<std::vector<unsigned char>>& packets, int frame_size,
      bool decode_fec);

  int valid() const { return valid_; }

  // Takes an encoded packet and decodes it. Returns the decoded audio
  // see documentation at:
  // https://mf4.xiph.org/jenkins/view/opus/job/opus/ws/doc/html/group__opus__decoder.html#ga7d1111f64c36027ddcb81799df9b3fc9
  std::vector<opus_int16> Decode(const std::vector<unsigned char>& packet,
                                 int frame_size, bool decode_fec);

  // Generates a dummy frame by passing nullptr to the underlying opus decode.
  std::vector<opus_int16> DecodeDummy(int frame_size);

 private:
  int num_channels_{};
  bool valid_{};
  internal::opus_uptr<OpusDecoder> decoder_;
};

}  // namespace opus

#endif

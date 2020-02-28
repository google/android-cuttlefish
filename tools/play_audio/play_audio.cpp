/*
 *
 * Copyright (C) 2018 The Android Open Source Project
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
#include "client_socket.h"
#include "sdl_wrapper.h"

#include "android-base/logging.h"
#include "gflags/gflags.h"
#include "opuscpp/opus_wrapper.h"
#include <SDL2/SDL.h>

#include <cstdint>
#include <tuple>
#include <vector>

DEFINE_int32(
    device_num, 1,
    "Cuttlefish device number, corresponding to username vsoc-## number");

namespace {
std::uint16_t AudioPort() {
  constexpr std::uint16_t kAudioStreamBasePort = 7444;
  std::uint16_t audio_port = kAudioStreamBasePort + (FLAGS_device_num - 1);
  return audio_port;
}

cfp::ClientSocket Connect() {
  const auto port = AudioPort();
  auto conn = cfp::ClientSocket{port};
  if (!conn.valid()) {
    LOG(FATAL) << "couldn't connect on port " << port;
  }
  return conn;
}

std::tuple<std::uint16_t, std::uint16_t> RecvHeader(cfp::ClientSocket* conn) {
  // creating variables because these must be received in order
  auto num_channels = conn->RecvUInt16();
  auto frame_rate = conn->RecvUInt16();
  LOG(INFO) << "\nnum_channels: " << num_channels
            << "\nframe_rate: " << frame_rate << '\n';
  return {num_channels, frame_rate};
}

// Returns frame_size and encoded audio
std::tuple<std::uint32_t, std::vector<unsigned char>> RecvEncodedAudio(
    cfp::ClientSocket* conn) {
  auto length = conn->RecvUInt32();
  auto frame_size = conn->RecvUInt32();
  auto encoded = conn->RecvAll(length);

  if (encoded.size() < length) {
    encoded.clear();
  }
  return {frame_size, std::move(encoded)};
}

void PlayDecodedAudio(cfp::SDLAudioDevice* audio_device,
                      const std::vector<opus_int16>& audio) {
  auto sz = audio.size() * sizeof audio[0];
  auto ret = audio_device->QueueAudio(audio.data(), sz);
  if (ret < 0) {
    LOG(ERROR) << "failed to queue audio: " << SDL_GetError() << '\n';
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  ::google::InitGoogleLogging(argv[0]);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  cfp::SDLLib sdl{};

  auto conn = Connect();
  const auto& [num_channels, frame_rate] = RecvHeader(&conn);

  auto audio_device = sdl.OpenAudioDevice(frame_rate, num_channels);
  auto dec =
      opus::Decoder{static_cast<std::uint32_t>(frame_rate), num_channels};
  CHECK(dec.valid()) << "Could not construct Decoder. Maybe bad frame_rate ("
                     << frame_rate <<") or num_channels (" << num_channels
                     << ")?";

  while (true) {
    CHECK(dec.valid()) << "decoder in invalid state";
    const auto& [frame_size, encoded] = RecvEncodedAudio(&conn);
    if (encoded.empty()) {
      break;
    }
    auto decoded = dec.Decode(encoded, frame_size, false);
    if (decoded.empty()) {
      break;
    }
    PlayDecodedAudio(&audio_device, decoded);
  }
}

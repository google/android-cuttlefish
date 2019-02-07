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

// For each client that connects initially a header is sent with the following,
// in this order, all as uint16_t in network-byte-order:
//  number of channels, frame rate
//
// Following, audio packets are sent as a uint32_t length (network byte order)
// indicating the number of bytes
// followed by the (opus) frame_size as a uint32_t
// followed by <length> bytes.

#include "common/libs/tcp_socket/tcp_socket.h"
#include "common/vsoc/lib/audio_data_region_view.h"
#include "common/vsoc/lib/circqueue_impl.h"
#include "common/vsoc/lib/vsoc_audio_message.h"
#include "host/frontend/stream_audio/opuscpp/opus_wrapper.h"
#include "host/libs/config/cuttlefish_config.h"

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include <arpa/inet.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

using vsoc::audio_data::AudioDataRegionView;

DEFINE_int32(port, 0, "port on which to serve audio.");

namespace {

// Read audio frames from the AudioDataRegionView
class AudioStreamer {
 public:
  cvd::Message MakeAudioDescriptionHeader() const {
    std::unique_lock guard(buffer_lock_);
    while (!audio_buffer_) {
      buffer_cv_.wait(guard);
    }

    const size_t num_channels = header_.frame_size / sizeof(opus_int16);
    return cvd::CreateMessage(static_cast<std::uint16_t>(num_channels),
                              static_cast<std::uint16_t>(header_.frame_rate));
  }

  std::uint32_t frame_rate() const {
    std::unique_lock guard(buffer_lock_);
    while (!audio_buffer_) {
      buffer_cv_.wait(guard);
    }
    return header_.frame_rate;
  }

  std::uint32_t num_channels() const {
    std::unique_lock guard(buffer_lock_);
    while (!audio_buffer_) {
      buffer_cv_.wait(guard);
    }
    return header_.frame_size / sizeof(opus_int16);
  }

  // Returns the frame id and audio frame
  std::tuple<std::int64_t, std::shared_ptr<const cvd::Message>> audio_buffer(
      std::int64_t previous_frame_num) const {
    std::unique_lock guard(buffer_lock_);
    while (header_.frame_num <= previous_frame_num) {
      buffer_cv_.wait(guard);
    }

    return {header_.frame_num, audio_buffer_};
  }

  void Update() {
    auto audio_data_rv =
        AudioDataRegionView::GetInstance(vsoc::GetDomain().c_str());
    auto worker = audio_data_rv->StartWorker();
    std::vector<char> new_buffer;

    while (true) {
      new_buffer.resize(new_buffer.capacity());

      auto [new_header, payload_size, audio_data] =
          NextAudioMessage(audio_data_rv, &new_buffer);

      LOG(DEBUG) << "stream " << new_header.stream_number << ", frame "
                 << new_header.frame_num << ", rate " << new_header.frame_rate
                 << ", channel_mask " << new_header.channel_mask << ", format "
                 << new_header.format << ", payload_size " << payload_size
                 << '\n';

      {
        std::lock_guard guard(buffer_lock_);
        CheckAudioConfigurationIsSame(new_header);
        header_ = new_header;
        audio_buffer_ = std::make_shared<const cvd::Message>(
            audio_data, audio_data + payload_size);
      }
      buffer_cv_.notify_all();
    }
  }

 private:
  struct AudioMessage {
    gce_audio_message header;
    std::size_t payload_size;
    const std::uint8_t* payload_data;
  };

  void ReadAudioMessage(AudioDataRegionView* audio_data_rv,
                        std::vector<char>* buffer) const {
    while (true) {
      auto read_size = audio_data_rv->data()->audio_queue.Read(
          audio_data_rv, buffer->data(), buffer->size());
      if (read_size == -ENOSPC) {
        DoubleSize(buffer);
      } else if (read_size < 0) {
        LOG(ERROR) << "CircularPacketQueue::Read returned " << read_size;
      } else {
        buffer->resize(read_size);
        return;
      }
    }
  }

  void DoubleSize(std::vector<char>* buffer) const {
    if (buffer->empty()) {
      buffer->resize(1);
    } else {
      buffer->resize(buffer->size() * 2);
    }
  }

  gce_audio_message GetHeaderFromBuffer(const std::vector<char>& buffer) const {
    gce_audio_message new_header{};
    CHECK_GE(buffer.size(), sizeof new_header);

    std::memcpy(&new_header, buffer.data(), sizeof new_header);
    CHECK_GT(new_header.stream_number, 0u);
    return new_header;
  }

  std::tuple<std::size_t, const std::uint8_t*> GetPayloadFromBuffer(
      const std::vector<char>& buffer) const {
    const auto payload_size = buffer.size() - sizeof(gce_audio_message);
    const auto* audio_data =
        reinterpret_cast<const std::uint8_t*>(buffer.data()) +
        sizeof(gce_audio_message);
    return {payload_size, audio_data};
  }

  AudioMessage NextAudioMessage(AudioDataRegionView* audio_data_rv,
                                std::vector<char>* buffer) const {
    while (true) {
      ReadAudioMessage(audio_data_rv, buffer);
      auto header = GetHeaderFromBuffer(*buffer);
      if (header.message_type == gce_audio_message::DATA_SAMPLES) {
        auto [payload_size, payload_data] = GetPayloadFromBuffer(*buffer);
        return {header, payload_size, payload_data};
      }
    }
  }

  void CheckAudioConfigurationIsSame(
      const gce_audio_message& new_header) const {
    if (audio_buffer_) {
      CHECK_EQ(header_.frame_size, new_header.frame_size)
          << "audio frame_size changed";
      CHECK_EQ(header_.frame_rate, new_header.frame_rate)
          << "audio frame_rate changed";
      CHECK_EQ(header_.stream_number, new_header.stream_number)
          << "audio stream_number changed";
    }
  }

  std::shared_ptr<const cvd::Message> audio_buffer_{};
  gce_audio_message header_{};
  mutable std::mutex buffer_lock_;
  mutable std::condition_variable buffer_cv_;
};

void HandleClient(AudioStreamer* audio_streamer,
                  cvd::ClientSocket client_socket) {
  auto num_channels = audio_streamer->num_channels();
  opus::Encoder enc(audio_streamer->frame_rate(),
                    audio_streamer->num_channels(), OPUS_APPLICATION_AUDIO);
  CHECK(enc.valid()) << "Could not construct Encoder. Maybe bad frame_rate ("
                     << audio_streamer->frame_rate() <<") or num_channels ("
                     << audio_streamer->num_channels() << ")?";

  auto header = audio_streamer->MakeAudioDescriptionHeader();
  client_socket.SendNoSignal(header);
  std::int64_t previous_frame_num = 0;

  while (!client_socket.closed()) {
    CHECK(enc.valid()) << "encoder in invalid state";
    auto [frame_num, audio_data] =
        audio_streamer->audio_buffer(previous_frame_num);
    previous_frame_num = frame_num;

    std::vector<opus_int16> pcm(audio_data->size() / sizeof(opus_int16));
    std::memcpy(pcm.data(), audio_data->data(), audio_data->size());
    // in opus terms "frame_size" is the number of unencoded samples per frame
    const std::uint32_t frame_size = pcm.size() / num_channels;
    auto encoded = enc.Encode(pcm, frame_size);
    for (auto&& p : encoded) {
      auto length_message =
          cvd::CreateMessage(static_cast<std::uint32_t>(p.size()));
      client_socket.SendNoSignal(length_message);
      client_socket.SendNoSignal(cvd::CreateMessage(frame_size));
      client_socket.SendNoSignal(p);
    }
  }
}

[[noreturn]] void AudioStreamerUpdateLoop(AudioStreamer* audio_streamer) {
  while (true) {
    audio_streamer->Update();
  }
}

[[noreturn]] void MainLoop() {
  AudioStreamer audio_streamer;
  std::thread audio_streamer_update_thread;
  auto server = cvd::ServerSocket(FLAGS_port);
  while (true) {
    LOG(INFO) << "waiting for client connection";
    auto client = server.Accept();
    LOG(INFO) << "client socket accepted";
    if (!audio_streamer_update_thread.joinable()) {
      audio_streamer_update_thread =
          std::thread{AudioStreamerUpdateLoop, &audio_streamer};
    }
    std::thread(HandleClient, &audio_streamer, std::move(client)).detach();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::SetUsageMessage(" ");
  google::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_port <= 0) {
    std::cerr << "--port must be specified.\n";
    return 1;
  }
  MainLoop();
}

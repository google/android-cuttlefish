//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <cinttypes>

#include <functional>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/audio_connector/buffers.h"
#include "host/libs/audio_connector/commands.h"
#include "host/libs/audio_connector/shm_layout.h"

namespace cuttlefish {

// Callbacks into objects implementing this interface will be made from the same
// thread that handles the connection fd. Implementations should make every
// effort to return immediately to avoid blocking the server's main loop.
class AudioServerExecutor {
 public:
  virtual ~AudioServerExecutor() = default;

  // Implementations must ensure each command is replied to before returning
  // from these functions. Failure to do so causes the program to abort.
  virtual void StreamsInfo(StreamInfoCommand& cmd) = 0;
  virtual void SetStreamParameters(StreamSetParamsCommand& cmd) = 0;
  virtual void PrepareStream(StreamControlCommand& cmd) = 0;
  virtual void ReleaseStream(StreamControlCommand& cmd) = 0;
  virtual void StartStream(StreamControlCommand& cmd) = 0;
  virtual void StopStream(StreamControlCommand& cmd) = 0;
  virtual void ChmapsInfo(ChmapInfoCommand& cmd) = 0;
  virtual void JacksInfo(JackInfoCommand& cmd) = 0;

  // Implementations must call buffer.SendStatus() before destroying the buffer
  // to notify the other side of the release of the buffer. Failure to do so
  // will cause the program to abort.
  virtual void OnPlaybackBuffer(TxBuffer buffer) = 0;
  virtual void OnCaptureBuffer(RxBuffer buffer) = 0;
};

class AudioClientConnection {
 public:
  static std::unique_ptr<AudioClientConnection> Create(
      SharedFD client_socket, uint32_t num_streams, uint32_t num_jacks,
      uint32_t num_chmaps, size_t tx_shm_len, size_t rx_shm_len);

  AudioClientConnection() = delete;
  AudioClientConnection(const AudioClientConnection&) = delete;

  AudioClientConnection& operator=(const AudioClientConnection&) = delete;

  // Allows the caller to react to commands sent by the client.
  bool ReceiveCommands(AudioServerExecutor& executor);
  // Allows the caller to react to IO buffers sent by the client.
  bool ReceivePlayback(AudioServerExecutor& executor);
  bool ReceiveCapture(AudioServerExecutor& executor);

  bool SendEvent(/*TODO*/);

 private:
  AudioClientConnection(ScopedMMap tx_shm, ScopedMMap rx_shm,
                        SharedFD control_socket, SharedFD event_socket,
                        SharedFD tx_socket, SharedFD rx_socket)
      : tx_shm_(std::move(tx_shm)),
        rx_shm_(std::move(rx_shm)),
        control_socket_(control_socket),
        event_socket_(event_socket),
        tx_socket_(tx_socket),
        rx_socket_(rx_socket) {}

  bool CmdReply(AudioStatus status, const void* data = nullptr,
                size_t size = 0);
  bool WithCommand(const virtio_snd_hdr* msg, size_t msg_len,
                   AudioServerExecutor& executor);

  ssize_t ReceiveMsg(SharedFD socket, void* buffer, size_t size);

  ScopedMMap tx_shm_;
  ScopedMMap rx_shm_;
  SharedFD control_socket_;
  SharedFD event_socket_;
  SharedFD tx_socket_;
  SharedFD rx_socket_;
};

class AudioServer {
 public:
  AudioServer(SharedFD server_socket) : server_socket_(server_socket) {}

  std::unique_ptr<AudioClientConnection> AcceptClient(uint32_t num_streams,
                                                      uint32_t num_jacks,
                                                      uint32_t num_chmaps,
                                                      size_t tx_shm_len,
                                                      size_t rx_shm_len);

 private:
  SharedFD server_socket_;
};

}  // namespace cuttlefish

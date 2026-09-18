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

#include "cuttlefish/host/commands/virtual_tuner_daemon/pcm_stream_server.h"

#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <ratio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/audio_generator.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace virtualtuner {
namespace {

constexpr mode_t kSocketMode = 0600;

using ChunkDuration =
    std::chrono::duration<int64_t, std::ratio<kChunkFrames, kSampleRate>>;

constexpr auto kMaxSchedulingLag = std::chrono::milliseconds(500);
constexpr int kPrebufferChunks = 2;

bool SendAll(const SharedFD& fd, const void* data, size_t size) {
  const char* cursor = static_cast<const char*>(data);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t written = fd->Send(cursor, remaining, MSG_NOSIGNAL);
    if (written > 0) {
      cursor += written;
      remaining -= static_cast<size_t>(written);
    } else if (written < 0 && fd->GetErrno() == EINTR) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

PcmStreamServer::PcmStreamServer(TunerState* tuner_state,
                                 std::string socket_path)
    : tuner_state_(tuner_state), socket_path_(std::move(socket_path)) {}

PcmStreamServer::~PcmStreamServer() { Stop(); }

Result<void> PcmStreamServer::Start() {
  if (is_running_) {
    return {};
  }

  ::unlink(socket_path_.c_str());
  server_fd_ = SharedFD::SocketLocalServer(
      socket_path_, /* is_abstract= */ false, SOCK_STREAM, kSocketMode);
  CF_EXPECT(server_fd_->IsOpen(),
            "Failed to open PCM streaming server UNIX socket at "
                << socket_path_ << ": " << server_fd_->StrError());

  LOG(INFO) << "PCM streaming server listening on " << socket_path_;
  is_running_ = true;
  accept_thread_ = std::thread(&PcmStreamServer::AcceptLoop, this);
  return {};
}

void PcmStreamServer::Stop() {
  if (!is_running_.exchange(false)) {
    return;
  }

  if (server_fd_->IsOpen()) {
    server_fd_->Shutdown(SHUT_RDWR);
    server_fd_->Close();
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  std::vector<std::unique_ptr<ClientSession>> sessions;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    sessions.swap(clients_);
  }
  for (const auto& session : sessions) {
    if (session->fd->IsOpen()) {
      session->fd->Shutdown(SHUT_RDWR);
    }
  }
  for (auto& session : sessions) {
    if (session->thread.joinable()) {
      session->thread.join();
    }
  }

  ::unlink(socket_path_.c_str());
  LOG(INFO) << "PCM streaming server stopped.";
}

void PcmStreamServer::AcceptLoop() {
  while (is_running_) {
    SharedFD client_fd = Fd::Accept(*server_fd_).value_or(Fd());
    if (!client_fd->IsOpen()) {
      if (!is_running_) {
        break;
      }
      continue;
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    ReapFinishedClients();
    if (clients_.size() >= kMaxConcurrentClients) {
      LOG(WARNING) << "Refusing PCM client: already serving " << clients_.size()
                   << " clients.";
      client_fd->Close();
      continue;
    }

    LOG(INFO) << "PCM streaming server accepted a client.";
    auto session = std::make_unique<ClientSession>();
    session->fd = std::move(client_fd);
    ClientSession* session_ptr = session.get();
    clients_.push_back(std::move(session));
    session_ptr->thread =
        std::thread(&PcmStreamServer::StreamClient, this, session_ptr);
  }
}

void PcmStreamServer::ReapFinishedClients() {
  std::erase_if(clients_, [](const std::unique_ptr<ClientSession>& session) {
    if (!session->finished.load(std::memory_order_acquire)) {
      return false;
    }
    if (session->thread.joinable()) {
      session->thread.join();
    }
    return true;
  });
}

void PcmStreamServer::StreamClient(ClientSession* session) {
  const SharedFD& client_fd = session->fd;
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels);

  const auto write_next_chunk = [&] {
    generator.GenerateChunk(buffer, tuner_state_->GetSnapshot());
    return SendAll(client_fd, buffer.data(), buffer.size() * sizeof(int16_t));
  };

  bool connected = true;
  for (int i = 0; i < kPrebufferChunks && connected && is_running_; ++i) {
    connected = write_next_chunk();
  }

  auto anchor = std::chrono::steady_clock::now();
  int64_t chunks_since_anchor = 0;

  while (connected && is_running_) {
    ++chunks_since_anchor;
    const auto deadline = anchor + ChunkDuration(chunks_since_anchor);
    std::this_thread::sleep_until(deadline);
    if (!is_running_) {
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now > deadline + kMaxSchedulingLag) {
      LOG(WARNING) << "PCM stream fell behind schedule by "
                   << std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - deadline)
                          .count()
                   << " ms; re-anchoring.";
      anchor = now;
      chunks_since_anchor = 0;
    }

    connected = write_next_chunk();
  }

  LOG(INFO) << "PCM client disconnected.";
  session->finished.store(true, std::memory_order_release);
}

}  // namespace virtualtuner
}  // namespace cuttlefish

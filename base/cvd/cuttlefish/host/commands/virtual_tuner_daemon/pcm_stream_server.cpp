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

#include <fcntl.h>
#include <sys/poll.h>
#include <sys/types.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <ratio>
#include <span>
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

using ChunkDuration =
    std::chrono::duration<int64_t, std::ratio<kChunkFrames, kSampleRate>>;

constexpr auto kMaxSchedulingLag = std::chrono::milliseconds(500);
constexpr int kPrebufferChunks = 2;

}  // namespace

PcmStreamServer::PcmStreamServer(TunerState& tuner_state, SharedFD server_fd)
    : tuner_state_(tuner_state), server_fd_(std::move(server_fd)) {}

PcmStreamServer::~PcmStreamServer() { Stop(); }

Result<void> PcmStreamServer::Start() {
  if (is_running_) {
    return {};
  }

  CF_EXPECT(server_fd_->IsOpen(),
            "Failed to start PCM streaming server: server_fd is not open");
  const int flags = server_fd_->Fcntl(F_GETFL, 0);
  CF_EXPECT(flags >= 0,
            "Failed to get flags on server_fd: " << server_fd_->StrError());
  CF_EXPECT(
      server_fd_->Fcntl(F_SETFL, flags | O_NONBLOCK) == 0,
      "Failed to set O_NONBLOCK on server_fd: " << server_fd_->StrError());
  stop_fd_ = CF_EXPECT(Fd::Event(0, EFD_NONBLOCK),
                       "Failed to create stop eventfd for PCM server");

  LOG(INFO) << "PCM streaming server running with inherited server fd.";
  is_running_ = true;
  accept_thread_ = std::thread(&PcmStreamServer::AcceptLoop, this);
  return {};
}

void PcmStreamServer::Stop() {
  if (!is_running_.exchange(false)) {
    return;
  }

  if (stop_fd_->IsOpen() && stop_fd_->EventfdWrite(1) != 0) {
    LOG(ERROR) << "Failed to signal PCM server stop eventfd: "
               << stop_fd_->StrError();
  }
  {
    std::lock_guard<std::mutex> lock(stop_mutex_);
  }
  stop_cv_.notify_all();
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  std::vector<std::unique_ptr<ClientSession>> sessions;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    sessions.swap(clients_);
  }
  for (std::unique_ptr<ClientSession>& session : sessions) {
    if (session->thread.joinable()) {
      session->thread.join();
    }
    if (session->fd->IsOpen()) {
      session->fd->Shutdown(SHUT_RDWR);
      session->fd->Close();
    }
  }
  if (stop_fd_->IsOpen()) {
    stop_fd_->Close();
  }

  LOG(INFO) << "PCM streaming server stopped.";
}

void PcmStreamServer::AcceptLoop() {
  constexpr size_t kServerPollIndex = 0;
  constexpr size_t kStopPollIndex = 1;
  std::vector<PollSharedFd> poll_fds = {
      {.fd = server_fd_, .events = POLLIN, .revents = 0},
      {.fd = stop_fd_, .events = POLLIN, .revents = 0},
  };

  while (is_running_) {
    poll_fds[kServerPollIndex].revents = 0;
    poll_fds[kStopPollIndex].revents = 0;
    const int poll_res = SharedFD::Poll(poll_fds, /*timeout=*/-1);
    if (poll_res < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG(ERROR) << "Failed to poll on PCM server socket: errno=" << errno;
      break;
    }
    if (poll_fds[kStopPollIndex].revents != 0 || !is_running_) {
      break;
    }
    if ((poll_fds[kServerPollIndex].revents & (POLLERR | POLLHUP | POLLNVAL)) !=
        0) {
      LOG(ERROR) << "Unexpected poll revents on PCM server socket: "
                 << poll_fds[kServerPollIndex].revents;
      break;
    }
    if ((poll_fds[kServerPollIndex].revents & POLLIN) == 0) {
      continue;
    }

    Result<Fd, int> client_res = Fd::Accept(*server_fd_);
    if (!is_running_) {
      break;
    }
    if (!client_res.has_value()) {
      const int accept_err = client_res.error().Value();
      if (accept_err == EAGAIN || accept_err == EWOULDBLOCK) {
        continue;
      }
      LOG(ERROR) << "Accept failed on PCM server socket: "
                 << client_res.error();
      continue;
    }
    SharedFD client_fd(std::move(*client_res));

    std::lock_guard<std::mutex> lock(clients_mutex_);
    ReapFinishedClients();
    if (clients_.size() >= kMaxConcurrentClients) {
      LOG(WARNING) << "Refusing PCM client: already serving " << clients_.size()
                   << " clients.";
      continue;
    }

    LOG(INFO) << "PCM streaming server accepted a client.";
    auto session = std::make_unique<ClientSession>();
    session->fd = std::move(client_fd);
    ClientSession* session_ptr = session.get();
    clients_.push_back(std::move(session));
    session_ptr->thread = std::thread(&PcmStreamServer::StreamClient, this,
                                      std::ref(*session_ptr));
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

bool PcmStreamServer::SendChunk(const SharedFD& client_fd,
                                std::span<const int16_t> samples) {
  constexpr size_t kClientPollIndex = 0;
  constexpr size_t kStopPollIndex = 1;
  std::vector<PollSharedFd> poll_fds = {
      {.fd = client_fd, .events = POLLOUT, .revents = 0},
      {.fd = stop_fd_, .events = POLLIN, .revents = 0},
  };

  const char* cursor = reinterpret_cast<const char*>(samples.data());
  size_t remaining = samples.size_bytes();
  while (remaining > 0 && is_running_) {
    const ssize_t written =
        client_fd->Send(cursor, remaining, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (written > 0) {
      cursor += written;
      remaining -= static_cast<size_t>(written);
      continue;
    }
    if (written == 0) {
      return false;
    }
    const int err = client_fd->GetErrno();
    if (err == EINTR) {
      continue;
    }
    if (err != EAGAIN && err != EWOULDBLOCK) {
      return false;
    }

    poll_fds[kClientPollIndex].revents = 0;
    poll_fds[kStopPollIndex].revents = 0;
    const int poll_res = SharedFD::Poll(poll_fds, /*timeout=*/-1);
    if (poll_res < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (poll_fds[kStopPollIndex].revents != 0 || !is_running_) {
      return false;
    }
    if ((poll_fds[kClientPollIndex].revents & (POLLERR | POLLHUP | POLLNVAL)) !=
        0) {
      return false;
    }
  }
  return remaining == 0;
}

void PcmStreamServer::StreamClient(ClientSession& session) {
  const SharedFD& client_fd = session.fd;
  AudioGenerator generator;
  std::vector<int16_t> buffer(kChunkFrames * kChannels);

  const auto write_next_chunk = [&]() -> bool {
    generator.GenerateChunk(buffer, tuner_state_.GetSnapshot());
    return SendChunk(client_fd, buffer);
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
    {
      std::unique_lock<std::mutex> lock(stop_mutex_);
      if (stop_cv_.wait_until(lock, deadline,
                              [&]() -> bool { return !is_running_; })) {
        break;
      }
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

  if (client_fd->IsOpen()) {
    client_fd->Shutdown(SHUT_RDWR);
    client_fd->Close();
  }
  LOG(INFO) << "PCM client disconnected.";
  session.finished.store(true, std::memory_order_release);
}

}  // namespace virtualtuner
}  // namespace cuttlefish

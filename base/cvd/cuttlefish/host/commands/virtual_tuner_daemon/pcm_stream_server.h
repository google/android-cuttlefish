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

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/virtual_tuner_daemon/tuner_state.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace virtualtuner {

inline constexpr size_t kMaxConcurrentClients = 8;

class PcmStreamServer {
 public:
  PcmStreamServer(TunerState* tuner_state, std::string socket_path);
  ~PcmStreamServer();

  PcmStreamServer(const PcmStreamServer&) = delete;
  PcmStreamServer& operator=(const PcmStreamServer&) = delete;

  Result<void> Start();
  void Stop();

  bool IsRunning() const { return is_running_; }
  const std::string& SocketPath() const { return socket_path_; }

 private:
  struct ClientSession {
    SharedFD fd;
    std::thread thread;
    std::atomic<bool> finished = false;
  };

  void AcceptLoop();
  void StreamClient(ClientSession* session);
  void ReapFinishedClients();

  TunerState* const tuner_state_;
  const std::string socket_path_;
  std::atomic<bool> is_running_ = false;
  SharedFD server_fd_;
  std::thread accept_thread_;
  std::mutex clients_mutex_;
  std::vector<std::unique_ptr<ClientSession>> clients_;
};

}  // namespace virtualtuner
}  // namespace cuttlefish

/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <sys/time.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
using VsockLockGuard = std::vector<std::unique_lock<std::mutex>>;

class VsockConnection {
 public:
  virtual ~VsockConnection();

  void Disconnect();
  bool IsConnected();
  void SetDisconnectCallback(std::function<void()> callback);
  bool DataAvailable();

  Result<int32_t> ReadInt32();
  Result<void> Read(std::vector<char>& data);
  Result<std::vector<char>> Read(size_t size);
  Result<void> ReadMessage(std::vector<char>& data);
  Result<std::vector<char>> ReadMessage();
  Result<Json::Value> ReadJsonMessage();
  std::future<Result<std::vector<char>>> ReadAsync(size_t size);
  std::future<Result<std::vector<char>>> ReadMessageAsync();
  std::future<Result<Json::Value>> ReadJsonMessageAsync();

  Result<void> Write(int32_t data);
  Result<void> Write(const char* data, unsigned int size);
  Result<void> Write(const std::vector<char>& data);
  Result<void> WriteMessage(const std::string& data);
  Result<void> WriteMessage(const std::vector<char>& data);
  Result<void> WriteMessage(const Json::Value& data);

 protected:
  enum class WaitStatus { kReady, kTimedOut, kDisconnected };

  static constexpr struct timeval default_timeout = {.tv_sec = 60,
                                                     .tv_usec = 0};

  VsockConnection();

  // Unsynchronized variants
  Result<int32_t> ReadInt32Internal();
  Result<void> ReadInternal(std::vector<char>& data);
  Result<void> WriteInternal(int32_t data);
  Result<void> WriteInternal(const char* data, unsigned int size);

  WaitStatus WaitForReadReady(struct timeval timeout = default_timeout);
  WaitStatus WaitForWriteReady(struct timeval timeout = default_timeout);
  void SignalDisconnect();
  void ClearDisconnect();
  VsockLockGuard DisconnectInternal();
  VsockLockGuard AcquireReadLock();
  VsockLockGuard AcquireWriteLock();
  VsockLockGuard AcquireReadWriteLocks();

  std::mutex read_mutex_;
  std::mutex write_mutex_;
  SharedFD fd_;
  SharedFD disconnect_notifier_;
  std::atomic_bool is_connected_;
  std::function<void()> disconnect_callback_;
};

class VsockClientConnection : public VsockConnection {
 public:
  bool Connect(unsigned int port, unsigned int cid, bool vhost_user);
  std::future<bool> ConnectAsync(unsigned int port, unsigned int cid,
                                 bool vhost_user);

 protected:
  virtual SharedFD CreateSocket(unsigned int port, unsigned int cid,
                                bool vhost_user);
};

class VsockServer : private VsockConnection {
 public:
  virtual ~VsockServer();
  Result<void> Start(unsigned int port, unsigned int cid,
                     std::optional<int> vhost_user_vsock_cid);
  void Stop();
  bool IsRunning();
  Result<std::unique_ptr<VsockConnection>> AcceptConnection();
  std::future<Result<std::unique_ptr<VsockConnection>>> AcceptConnectionAsync();

 protected:
  virtual SharedFD CreateSocket(unsigned int port, unsigned int cid,
                                std::optional<int> vhost_user_vsock_cid);

 private:
  class VsockServerConnection : public VsockConnection {
    VsockServerConnection(SharedFD vsock_fd);
    friend VsockServer;
  };
};

}  // namespace cuttlefish

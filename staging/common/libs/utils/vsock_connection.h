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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class VsockConnection {
 public:
  virtual ~VsockConnection();
  virtual bool Connect(unsigned int port, unsigned int cid,
                       std::optional<int> vhost_user_vsock_cid) = 0;
  virtual void Disconnect();
  std::future<bool> ConnectAsync(unsigned int port, unsigned int cid,
                                 std::optional<int> vhost_user_vsock_cid);
  void SetDisconnectCallback(std::function<void()> callback);

  bool IsConnected();
  bool DataAvailable();
  int32_t Read();
  bool Read(std::vector<char>& data);
  std::vector<char> Read(size_t size);
  std::future<std::vector<char>> ReadAsync(size_t size);

  bool ReadMessage(std::vector<char>& data);
  std::vector<char> ReadMessage();
  std::future<std::vector<char>> ReadMessageAsync();
  Json::Value ReadJsonMessage();
  std::future<Json::Value> ReadJsonMessageAsync();

  bool Write(int32_t data);
  bool Write(const char* data, unsigned int size);
  bool Write(const std::vector<char>& data);
  bool WriteMessage(const std::string& data);
  bool WriteMessage(const std::vector<char>& data);
  bool WriteMessage(const Json::Value& data);
  bool WriteStrides(const char* data, unsigned int size,
                    unsigned int num_strides, int stride_size);

 protected:
  std::recursive_mutex read_mutex_;
  std::recursive_mutex write_mutex_;
  std::function<void()> disconnect_callback_;
  SharedFD fd_;
};

class VsockClientConnection : public VsockConnection {
 public:
  // the value of vhost_user_vsock_cid isn't actually used, it works like bool,
  // so any value except nullopt means true
  bool Connect(unsigned int port, unsigned int cid,
               std::optional<int> vhost_user) override;
};

class VsockServerConnection : public VsockConnection {
 public:
  virtual ~VsockServerConnection();
  void ServerShutdown();
  bool Connect(unsigned int port, unsigned int cid,
               std::optional<int> vhost_user_vsock_cid) override;

 private:
  SharedFD server_fd_;
};

}  // namespace cuttlefish

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

#include "common/libs/utils/vsock_connection.h"

#include <sys/socket.h>
#include <sys/time.h>

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

VsockConnection::~VsockConnection() { Disconnect(); }

std::future<bool> VsockConnection::ConnectAsync(unsigned int port,
                                                unsigned int cid,
                                                bool vhost_user) {
  return std::async(std::launch::async, [this, port, cid, vhost_user]() {
    return Connect(port, cid, vhost_user);
  });
}

void VsockConnection::Disconnect() {
  // We need to serialize all accesses to the SharedFD.
  std::lock_guard<std::recursive_mutex> read_lock(read_mutex_);
  std::lock_guard<std::recursive_mutex> write_lock(write_mutex_);

  LOG(INFO) << "Disconnecting with fd status:" << fd_->StrError();
  fd_->Shutdown(SHUT_RDWR);
  if (disconnect_callback_) {
    disconnect_callback_();
  }
  fd_->Close();
}

void VsockConnection::SetDisconnectCallback(std::function<void()> callback) {
  disconnect_callback_ = callback;
}

bool VsockConnection::IsConnected() {
  // We need to serialize all accesses to the SharedFD.
  std::lock_guard<std::recursive_mutex> read_lock(read_mutex_);
  std::lock_guard<std::recursive_mutex> write_lock(write_mutex_);

  return fd_->IsOpen();
}

bool VsockConnection::DataAvailable() {
  SharedFDSet read_set;

  // We need to serialize all accesses to the SharedFD.
  std::lock_guard<std::recursive_mutex> read_lock(read_mutex_);
  std::lock_guard<std::recursive_mutex> write_lock(write_mutex_);

  read_set.Set(fd_);
  struct timeval timeout = {0, 0};
  return Select(&read_set, nullptr, nullptr, &timeout) > 0;
}

int32_t VsockConnection::Read() {
  std::lock_guard<std::recursive_mutex> lock(read_mutex_);
  int32_t result;
  if (ReadExactBinary(fd_, &result) != sizeof(result)) {
    Disconnect();
    return 0;
  }
  return result;
}

bool VsockConnection::Read(std::vector<char>& data) {
  std::lock_guard<std::recursive_mutex> lock(read_mutex_);
  return ReadExact(fd_, &data) == data.size();
}

std::vector<char> VsockConnection::Read(size_t size) {
  if (size == 0) {
    return {};
  }
  std::lock_guard<std::recursive_mutex> lock(read_mutex_);
  std::vector<char> result(size);
  if (ReadExact(fd_, &result) != size) {
    Disconnect();
    return {};
  }
  return result;
}

std::future<std::vector<char>> VsockConnection::ReadAsync(size_t size) {
  return std::async(std::launch::async, [this, size]() { return Read(size); });
}

// Message format is buffer size followed by buffer data
std::vector<char> VsockConnection::ReadMessage() {
  std::lock_guard<std::recursive_mutex> lock(read_mutex_);
  auto size = Read();
  if (size < 0) {
    Disconnect();
    return {};
  }
  return Read(size);
}

bool VsockConnection::ReadMessage(std::vector<char>& data) {
  std::lock_guard<std::recursive_mutex> lock(read_mutex_);
  auto size = Read();
  if (size < 0) {
    Disconnect();
    return false;
  }
  data.resize(size);
  return Read(data);
}

std::future<std::vector<char>> VsockConnection::ReadMessageAsync() {
  return std::async(std::launch::async, [this]() { return ReadMessage(); });
}

Json::Value VsockConnection::ReadJsonMessage() {
  auto msg = ReadMessage();
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value json_msg;
  std::string errors;
  if (!reader->parse(msg.data(), msg.data() + msg.size(), &json_msg, &errors)) {
    return {};
  }
  return json_msg;
}

std::future<Json::Value> VsockConnection::ReadJsonMessageAsync() {
  return std::async(std::launch::async, [this]() { return ReadJsonMessage(); });
}

bool VsockConnection::Write(int32_t data) {
  std::lock_guard<std::recursive_mutex> lock(write_mutex_);
  if (WriteAllBinary(fd_, &data) != sizeof(data)) {
    Disconnect();
    return false;
  }
  return true;
}

bool VsockConnection::Write(const char* data, unsigned int size) {
  std::lock_guard<std::recursive_mutex> lock(write_mutex_);
  if (WriteAll(fd_, data, size) != size) {
    Disconnect();
    return false;
  }
  return true;
}

bool VsockConnection::Write(const std::vector<char>& data) {
  return Write(data.data(), data.size());
}

// Message format is buffer size followed by buffer data
bool VsockConnection::WriteMessage(const std::string& data) {
  return Write(data.size()) && Write(data.c_str(), data.length());
}

bool VsockConnection::WriteMessage(const std::vector<char>& data) {
  std::lock_guard<std::recursive_mutex> lock(write_mutex_);
  return Write(data.size()) && Write(data);
}

bool VsockConnection::WriteMessage(const Json::Value& data) {
  Json::StreamWriterBuilder factory;
  std::string message_str = Json::writeString(factory, data);
  return WriteMessage(message_str);
}

bool VsockConnection::WriteStrides(const char* data, unsigned int size,
                                   unsigned int num_strides, int stride_size) {
  const char* src = data;
  for (unsigned int i = 0; i < num_strides; ++i, src += stride_size) {
    if (!Write(src, size)) {
      return false;
    }
  }
  return true;
}

bool VsockClientConnection::Connect(unsigned int port, unsigned int cid,
                                    bool vhost_user) {
  fd_ = SharedFD::VsockClient(cid, port, SOCK_STREAM, vhost_user);
  if (!fd_->IsOpen()) {
    LOG(ERROR) << "Failed to connect:" << fd_->StrError();
  }
  return fd_->IsOpen();
}

VsockServerConnection::~VsockServerConnection() { ServerShutdown(); }

void VsockServerConnection::ServerShutdown() {
  if (server_fd_->IsOpen()) {
    LOG(INFO) << __FUNCTION__
              << ": server fd status:" << server_fd_->StrError();
    server_fd_->Shutdown(SHUT_RDWR);
    server_fd_->Close();
  }
}

bool VsockServerConnection::Connect(unsigned int port, unsigned int cid,
                                    bool vhost_user) {
  if (!server_fd_->IsOpen()) {
    server_fd_ =
        cuttlefish::SharedFD::VsockServer(port, SOCK_STREAM, vhost_user, cid);
  }
  if (server_fd_->IsOpen()) {
    fd_ = SharedFD::Accept(*server_fd_);
    return fd_->IsOpen();
  } else {
    return false;
  }
}

}  // namespace cuttlefish

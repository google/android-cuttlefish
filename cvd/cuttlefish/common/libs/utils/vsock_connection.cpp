/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <json/json.h>

#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

VsockConnection::VsockConnection() {
  disconnect_notifier_ = cuttlefish::SharedFD::Event();
}

VsockConnection::~VsockConnection() { Disconnect(); }

void VsockConnection::Disconnect() {
  // We do not store the lock guard so the locks are immediately dropped as we
  // don't want users mucking around with the internals locks
  DisconnectInternal();
}

// Signals to threads performing IO that we are trying to disconnect so they
// can release any locks they have
void VsockConnection::SignalDisconnect() {
  is_connected_ = false;
  if (disconnect_notifier_->IsOpen()) {
    disconnect_notifier_->EventfdWrite(1);
  }
}

// Clears the disconnect_notifier_ eventfd if an event is pending
void VsockConnection::ClearDisconnect() {
  SharedFDSet fd_set;
  fd_set.Set(disconnect_notifier_);
  // The FD only needs to be cleared an event is pending so timeout immediately
  struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
  if (Select(&fd_set, nullptr, nullptr, &timeout) > 0) {
    eventfd_t event_val;
    disconnect_notifier_->EventfdRead(&event_val);
  }
}

// Disconnect and close FDs and returns with the locks held.
VsockLockGuard VsockConnection::DisconnectInternal() {
  SignalDisconnect();
  VsockLockGuard locks = AcquireReadWriteLocks();
  fd_->Shutdown(SHUT_RDWR);
  if (disconnect_callback_) {
    disconnect_callback_();
  }
  fd_->Close();
  // Clear the disconnect eventfd so we can reconnect if needed
  ClearDisconnect();
  return locks;
}

void VsockConnection::SetDisconnectCallback(std::function<void()> callback) {
  disconnect_callback_ = callback;
}

bool VsockConnection::IsConnected() { return is_connected_; }

VsockConnection::WaitStatus VsockConnection::WaitForReadReady(
    struct timeval timeout) {
  SharedFDSet read_set;
  read_set.Set(fd_);
  read_set.Set(disconnect_notifier_);
  auto result = Select(&read_set, nullptr, nullptr, &timeout);
  if (read_set.IsSet(disconnect_notifier_) || result == -1) {
    return VsockConnection::WaitStatus::kDisconnected;
  } else if (read_set.IsSet(fd_)) {
    return VsockConnection::WaitStatus::kReady;
  } else {
    return VsockConnection::WaitStatus::kTimedOut;
  }
}

VsockConnection::WaitStatus VsockConnection::WaitForWriteReady(
    struct timeval timeout) {
  SharedFDSet read_set;
  SharedFDSet write_set;
  write_set.Set(fd_);
  read_set.Set(disconnect_notifier_);
  auto result = Select(&read_set, &write_set, nullptr, &timeout);
  if (read_set.IsSet(disconnect_notifier_) || result == -1) {
    return VsockConnection::WaitStatus::kDisconnected;
  } else if (write_set.IsSet(fd_)) {
    return VsockConnection::WaitStatus::kReady;
  } else {
    return VsockConnection::WaitStatus::kTimedOut;
  }
}

VsockLockGuard VsockConnection::AcquireReadWriteLocks() {
  VsockLockGuard locks;
  locks.push_back(std::unique_lock(read_mutex_));
  locks.push_back(std::unique_lock(write_mutex_));
  return locks;
}

VsockLockGuard VsockConnection::AcquireReadLock() {
  VsockLockGuard lock_guard;
  lock_guard.push_back(std::unique_lock(read_mutex_));
  return lock_guard;
}

VsockLockGuard VsockConnection::AcquireWriteLock() {
  VsockLockGuard lock_guard;
  lock_guard.push_back(std::unique_lock(write_mutex_));
  return lock_guard;
}

bool VsockConnection::DataAvailable() {
  auto read_lock = AcquireReadLock();
  struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
  return WaitForReadReady(timeout) == WaitStatus::kReady;
}

Result<int32_t> VsockConnection::ReadInt32Internal() {
  int32_t result;
  auto data = std::vector<char>(sizeof(result));
  CF_EXPECT(ReadInternal(data));
  result = *(int32_t*)data.data();
  return result;
}

Result<void> VsockConnection::ReadInternal(std::vector<char>& data) {
  unsigned int total_read = 0;
  while (total_read < data.size()) {
    if (!IsConnected()) {
      return CF_ERR("Cannot read from disconnected socket.");
    }
    auto status = WaitForReadReady();
    if (status == VsockConnection::WaitStatus::kDisconnected) {
      return CF_ERR("Cannot read from disconnected socket.");
    } else if (status == VsockConnection::WaitStatus::kReady) {
      auto bytes_read = fd_->Read(&data[total_read], data.size() - total_read);
      if (bytes_read < 0 &&
          (fd_->GetErrno() == EAGAIN || fd_->GetErrno() == EWOULDBLOCK)) {
        continue;
      } else if (bytes_read <= 0) {
        SignalDisconnect();
        return CF_ERR("Failed to read from socket: ") << fd_->StrError();
      }
      total_read += bytes_read;
    }
  }
  CF_EXPECT_EQ(total_read, data.size());
  return {};
}

Result<int32_t> VsockConnection::ReadInt32() {
  auto read_lock = AcquireReadLock();
  return ReadInt32Internal();
}

Result<void> VsockConnection::Read(std::vector<char>& data) {
  auto read_lock = AcquireReadLock();
  return ReadInternal(data);
}

Result<std::vector<char>> VsockConnection::Read(size_t size) {
  auto data = std::vector<char>(size);
  auto read_locks = AcquireReadLock();
  CF_EXPECT(ReadInternal(data));
  return data;
}

std::future<Result<std::vector<char>>> VsockConnection::ReadAsync(size_t size) {
  return std::async(std::launch::async, [this, size]() { return Read(size); });
}

Result<std::vector<char>> VsockConnection::ReadMessage() {
  std::vector<char> data;
  CF_EXPECT(ReadMessage(data));
  return data;
}

// Message format is buffer size followed by buffer data
Result<void> VsockConnection::ReadMessage(std::vector<char>& data) {
  auto read_lock = AcquireReadLock();
  // Check the size of the incoming message
  auto size = CF_EXPECT(ReadInt32Internal());
  data.resize(size);
  return ReadInternal(data);
}

std::future<Result<std::vector<char>>> VsockConnection::ReadMessageAsync() {
  return std::async(std::launch::async, [this]() { return ReadMessage(); });
}

Result<Json::Value> VsockConnection::ReadJsonMessage() {
  auto msg = CF_EXPECT(ReadMessage());
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value json_msg;
  std::string errors;
  if (!reader->parse(msg.data(), msg.data() + msg.size(), &json_msg, &errors)) {
    return CF_ERR("Unable to parse JSON");
  }
  return json_msg;
}

std::future<Result<Json::Value>> VsockConnection::ReadJsonMessageAsync() {
  return std::async(std::launch::async, [this]() { return ReadJsonMessage(); });
}

Result<void> VsockConnection::WriteInternal(const char* data,
                                            unsigned int size) {
  unsigned int total_written = 0;
  while (total_written < size) {
    if (!IsConnected()) {
      return CF_ERR("Cannot write to disconnected socket.");
    }
    auto status = WaitForWriteReady();
    if (status == VsockConnection::WaitStatus::kDisconnected) {
      break;
    } else if (status == VsockConnection::WaitStatus::kReady) {
      auto bytes_written =
          fd_->Write(&data[total_written], size - total_written);
      if (bytes_written < 0 &&
          (fd_->GetErrno() == EAGAIN || fd_->GetErrno() == EWOULDBLOCK)) {
        continue;
      } else if (bytes_written <= 0) {
        SignalDisconnect();
        return CF_ERR("Failed to write from socket: ") << fd_->StrError();
      }
      total_written += bytes_written;
    }
  }
  CF_EXPECT(total_written == size);
  return {};
}

Result<void> VsockConnection::WriteInternal(int32_t data) {
  return WriteInternal((char*)&data, sizeof(data));
}

Result<void> VsockConnection::Write(int32_t data) {
  auto write_lock = AcquireWriteLock();
  return WriteInternal(data);
}

Result<void> VsockConnection::Write(const char* data, unsigned int size) {
  auto write_lock = AcquireWriteLock();
  return WriteInternal(data, size);
}

Result<void> VsockConnection::Write(const std::vector<char>& data) {
  auto write_lock = AcquireWriteLock();
  return WriteInternal(data.data(), data.size());
}

// Message format is buffer size followed by buffer data
Result<void> VsockConnection::WriteMessage(const std::string& data) {
  auto write_lock = AcquireWriteLock();
  CF_EXPECT(WriteInternal(data.length()));
  return WriteInternal(data.c_str(), data.length());
}

Result<void> VsockConnection::WriteMessage(const std::vector<char>& data) {
  auto write_lock = AcquireReadWriteLocks();
  CF_EXPECT(WriteInternal(data.size()));
  return WriteInternal(data.data(), data.size());
}

Result<void> VsockConnection::WriteMessage(const Json::Value& data) {
  Json::StreamWriterBuilder factory;
  std::string message_str = Json::writeString(factory, data);
  return WriteMessage(message_str);
}

bool VsockClientConnection::Connect(unsigned int port, unsigned int cid,
                                    bool vhost_user) {
  auto locks = DisconnectInternal();
  fd_ = CreateSocket(port, cid, vhost_user);
  is_connected_ = fd_->IsOpen();
  return IsConnected();
}

SharedFD VsockClientConnection::CreateSocket(unsigned int port,
                                             unsigned int cid,
                                             bool vhost_user) {
  return SharedFD::VsockClient(cid, port, SOCK_STREAM | SOCK_NONBLOCK,
                               vhost_user);
}

std::future<bool> VsockClientConnection::ConnectAsync(unsigned int port,
                                                      unsigned int cid,
                                                      bool vhost_user) {
  return std::async(std::launch::async, [this, port, cid, vhost_user]() {
    return Connect(port, cid, vhost_user);
  });
}

VsockServer::~VsockServer() { Stop(); }

Result<void> VsockServer::Start(unsigned int port, unsigned int cid,
                                std::optional<int> vhost_user_vsock_cid) {
  Stop();
  fd_ = CreateSocket(port, cid, vhost_user_vsock_cid);
  is_connected_ = fd_->IsOpen();
  CF_EXPECT(is_connected_.load());
  return {};
}

void VsockServer::Stop() { Disconnect(); }

bool VsockServer::IsRunning() { return fd_->IsOpen() && IsConnected(); }

SharedFD VsockServer::CreateSocket(unsigned int port, unsigned int cid,
                                   std::optional<int> vhost_user_vsock_cid) {
  return SharedFD::VsockServer(port, SOCK_STREAM | SOCK_NONBLOCK,
                               vhost_user_vsock_cid, cid);
}

Result<std::unique_ptr<VsockConnection>> VsockServer::AcceptConnection() {
  auto read_lock = AcquireReadLock();
  while (IsRunning()) {
    auto wait_status = WaitForReadReady();
    if (wait_status == WaitStatus::kReady) {
      SharedFD connection_fd = SharedFD::Accept(*fd_);
      if (connection_fd->IsOpen()) {
        return std::unique_ptr<VsockConnection>(
            new VsockServerConnection(connection_fd));
      }
    } else if (wait_status == WaitStatus::kDisconnected) {
      Disconnect();
    }
  }

  return CF_ERR("Server disconnected before client connected");
}

std::future<Result<std::unique_ptr<VsockConnection>>>
VsockServer::AcceptConnectionAsync() {
  return std::async(std::launch::async,
                    [this]() { return AcceptConnection(); });
}

VsockServer::VsockServerConnection::VsockServerConnection(SharedFD vsock_fd) {
  fd_ = vsock_fd;
  is_connected_ = vsock_fd->IsOpen();
}

}  // namespace cuttlefish

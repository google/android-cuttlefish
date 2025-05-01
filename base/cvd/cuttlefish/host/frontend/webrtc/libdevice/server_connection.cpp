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
//

#include "host/frontend/webrtc/libdevice/server_connection.h"

#include <atomic>
#include <mutex>
#include <thread>

#include <android-base/logging.h>
#include <json/json.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

namespace cuttlefish {
namespace webrtc_streaming {

// ServerConnection over Unix socket
class UnixServerConnection : public ServerConnection {
 public:
  UnixServerConnection(const std::string& addr,
                       std::weak_ptr<ServerConnectionObserver> observer);
  ~UnixServerConnection() override;

  bool Send(const Json::Value& msg) override;

 private:
  void Connect() override;
  void StopThread();
  void ReadLoop();

  const std::string addr_;
  SharedFD conn_;
  std::mutex write_mtx_;
  std::weak_ptr<ServerConnectionObserver> observer_;
  // The event fd must be declared before the thread to ensure it's initialized
  // before the thread starts and is safe to be accessed from it.
  SharedFD thread_notifier_;
  std::atomic_bool running_ = false;
  std::thread thread_;
};

std::unique_ptr<ServerConnection> ServerConnection::Connect(
    const ServerConfig& conf,
    std::weak_ptr<ServerConnectionObserver> observer) {
  std::unique_ptr<ServerConnection> ret(
      new UnixServerConnection(conf.addr, observer));
  ret->Connect();
  return ret;
}

void ServerConnection::Reconnect() { Connect(); }

// UnixServerConnection implementation

UnixServerConnection::UnixServerConnection(
    const std::string& addr, std::weak_ptr<ServerConnectionObserver> observer)
    : addr_(addr), observer_(observer) {}

UnixServerConnection::~UnixServerConnection() {
  StopThread();
}

bool UnixServerConnection::Send(const Json::Value& msg) {
  Json::StreamWriterBuilder factory;
  auto str = Json::writeString(factory, msg);
  std::lock_guard<std::mutex> lock(write_mtx_);
  auto res =
      conn_->Send(reinterpret_cast<const uint8_t*>(str.c_str()), str.size(), 0);
  if (res < 0) {
    LOG(ERROR) << "Failed to send data to signaling server: "
               << conn_->StrError();
    // Don't call OnError() here, the receiving thread probably did it already
    // or is about to do it.
  }
  // A SOCK_SEQPACKET unix socket will send the entire message or fail, but it
  // won't send a partial message.
  return res == str.size();
}

void UnixServerConnection::Connect() {
  // The thread could be running if this is a Reconnect
  StopThread();

  conn_ = SharedFD::SocketLocalClient(addr_, false, SOCK_SEQPACKET);
  if (!conn_->IsOpen()) {
    LOG(ERROR) << "Failed to connect to unix socket: " << conn_->StrError();
    if (auto o = observer_.lock(); o) {
      o->OnError("Failed to connect to unix socket");
    }
    return;
  }
  thread_notifier_ = SharedFD::Event();
  if (!thread_notifier_->IsOpen()) {
    LOG(ERROR) << "Failed to create eventfd for background thread: "
               << thread_notifier_->StrError();
    if (auto o = observer_.lock(); o) {
      o->OnError("Failed to create eventfd for background thread");
    }
    return;
  }
  if (auto o = observer_.lock(); o) {
    o->OnOpen();
  }
  // Start the thread
  running_ = true;
  thread_ = std::thread([this](){ReadLoop();});
}

void UnixServerConnection::StopThread() {
  running_ = false;
  if (!thread_notifier_->IsOpen()) {
    // The thread won't be running if this isn't open
    return;
  }
  if (thread_notifier_->EventfdWrite(1) < 0) {
    LOG(ERROR) << "Failed to notify background thread, this thread may block";
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

void UnixServerConnection::ReadLoop() {
  if (!thread_notifier_->IsOpen()) {
    LOG(ERROR) << "The UnixServerConnection's background thread is unable to "
                  "receive notifications so it can't run";
    return;
  }
  std::vector<uint8_t> buffer(4096, 0);
  while (running_) {
    SharedFDSet rset;
    rset.Set(thread_notifier_);
    rset.Set(conn_);
    auto res = Select(&rset, nullptr, nullptr, nullptr);
    if (res < 0) {
      LOG(ERROR) << "Failed to select from background thread";
      break;
    }
    if (rset.IsSet(thread_notifier_)) {
      eventfd_t val;
      auto res = thread_notifier_->EventfdRead(&val);
      if (res < 0) {
        LOG(ERROR) << "Error reading from event fd: "
                   << thread_notifier_->StrError();
        break;
      }
    }
    if (rset.IsSet(conn_)) {
      auto size = conn_->Recv(buffer.data(), 0, MSG_TRUNC | MSG_PEEK);
      if (size > buffer.size()) {
        // Enlarge enough to accommodate size bytes and be a multiple of 4096
        auto new_size = (size + 4095) & ~4095;
        buffer.resize(new_size);
      }
      auto res = conn_->Recv(buffer.data(), buffer.size(), MSG_TRUNC);
      if (res < 0) {
        LOG(ERROR) << "Failed to read from server: " << conn_->StrError();
        if (auto observer = observer_.lock(); observer) {
          observer->OnError(conn_->StrError());
        }
        return;
      }
      if (res == 0) {
        auto observer = observer_.lock();
        if (observer) {
          observer->OnClose();
        }
        break;
      }
      auto observer = observer_.lock();
      if (observer) {
        observer->OnReceive(buffer.data(), res, false);
      }
    }
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish

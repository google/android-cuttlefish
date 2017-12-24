/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "common/libs/wifi/wr_client.h"

#include <glog/logging.h>

namespace cvd {
namespace {
const int kMaxSupportedPacketSize = getpagesize();
}  // namespace
WRClient::WRClient(const std::string& address) : address_(address) {}

bool WRClient::Init() {
  // Sadly, we can't use SharedFD, because we need access to raw file
  // descriptor.

  struct sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path + 1, address_.c_str(), address_.size());
  socklen_t len = offsetof(struct sockaddr_un, sun_path) + address_.size() + 1;
  socket_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (socket_ < 0) {
    LOG(ERROR) << "socket() failed: " << strerror(errno);
    return false;
  }

  auto res = connect(socket_, reinterpret_cast<sockaddr*>(&addr), len);
  if (res < 0) {
    LOG(ERROR) << "Could not connect to wifi router: " << strerror(errno);
    return false;
  }

  return true;
}

void WRClient::Send(Cmd* msg) {
  std::lock_guard<std::mutex> guard(in_flight_mutex_);
  // Make sure to execute this while in critical section to ensure we have time
  // to set up seq number & callback before we receive response.
  auto hdr = nlmsg_hdr(msg->Msg());
  int seq = in_flight_last_seq_++;
  // Do not use 0 for sequence numbers. 0 is reserved for async notifications.
  if (!in_flight_last_seq_) in_flight_last_seq_ = 1;

  hdr->nlmsg_seq = seq;
  send(socket_, hdr, hdr->nlmsg_len, MSG_NOSIGNAL);
  in_flight_[seq] = msg;
}

// Handle asynchronous messages & responses from netlink.
void WRClient::HandleResponses() {
  std::unique_ptr<uint8_t[]> buf(new uint8_t[kMaxSupportedPacketSize]);

  auto size = recv(socket_, buf.get(), kMaxSupportedPacketSize, 0);
  if (size <= 0) {
    LOG(FATAL) << "No data from WIFI Router - likely dead: " << strerror(errno);
    return;
  }

  auto hdr = reinterpret_cast<nlmsghdr*>(buf.get());
  if (static_cast<uint32_t>(size) != hdr->nlmsg_len) {
    LOG(FATAL) << "Malformed message from WIFI Router.";
    return;
  }

  int seq = hdr->nlmsg_seq;
  std::unique_ptr<nl_msg, void (*)(nl_msg*)> nlmsg(
      nlmsg_convert(hdr), [](nl_msg* m) { nlmsg_free(m); });

  // Find & invoke corresponding callback, if any.
  std::lock_guard<std::mutex> guard(in_flight_mutex_);
  auto pos = in_flight_.find(seq);
  if (pos != in_flight_.end()) {
    if (pos->second->OnResponse(nlmsg.get())) {
      // Erase command if reports it's done.
      in_flight_.erase(seq);
    }
  } else if (default_handler_) {
    default_handler_(nlmsg.get());
  }
}

void WRClient::SetDefaultHandler(std::function<void(nl_msg*)> cb) {
  std::lock_guard<std::mutex> guard(in_flight_mutex_);
  default_handler_ = std::move(cb);
}

int WRClient::Sock() const { return socket_; }

}  // namespace cvd

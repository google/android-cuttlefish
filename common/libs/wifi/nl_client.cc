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
#include "host/commands/wifid/nl_client.h"

#include <glog/logging.h>

namespace avd {

NlClient::NlClient(int nl_type)
    : nl_type_(nl_type),
      callback_(nullptr, [](nl_cb* cb) { free(cb); }),
      sock_(nullptr, [](nl_sock* sock) { free(sock); }) {}

bool NlClient::Init() {
  // Set up netlink callbacks.
  callback_.reset(nl_cb_alloc(NL_CB_CUSTOM));
  if (!callback_) {
    LOG(ERROR) << "Could not create netlink callback.";
    return false;
  }

  // Register callback that will receive asynchronous messages from netlink.
  nl_cb_set(callback_.get(), NL_CB_MSG_IN, NL_CB_CUSTOM,
            [](nl_msg* msg, void* data) {
              NlClient* self = static_cast<NlClient*>(data);
              return self->OnResponse(msg);
            },
            this);

  // Open Netlink target.
  sock_.reset(nl_socket_alloc_cb(callback_.get()));
  if (!sock_) {
    LOG(ERROR) << "Could not create netlink socket. Are you root?";
    return false;
  }

  if (nl_connect(sock_.get(), nl_type_) < 0) {
    LOG(ERROR) << "Could not connect to netlink. Are you root?";
    return false;
  }

  return true;
}

void NlClient::Send(Cmd* msg) {
  std::lock_guard<std::mutex> guard(in_flight_mutex_);
  // nl_send_auto sets sequence number (if defaults to NL_AUTO_SEQ).
  // Make sure to execute this while in critical section to ensure we have time
  // to set up callback before we receive response.
  nl_send_auto(sock_.get(), msg->Msg());
  auto seq = nlmsg_hdr(msg->Msg())->nlmsg_seq;
  in_flight_[seq] = msg;
}

// Handle asynchronous messages & responses from netlink.
int NlClient::OnResponse(nl_msg* msg) {
  nlmsghdr* header = nlmsg_hdr(msg);
  int seq = header->nlmsg_seq;

  // Find & invoke corresponding callback, if any.
  std::lock_guard<std::mutex> guard(in_flight_mutex_);
  auto pos = in_flight_.find(seq);
  if (pos != in_flight_.end()) {
    if (pos->second->OnResponse(msg)) {
      // Erase command if reports it's done.
      in_flight_.erase(seq);
    }
  } else if (default_handler_) {
    default_handler_(msg);
  }

  return NL_OK;
}

void NlClient::SetDefaultHandler(std::function<void(nl_msg*)> cb) {
  std::lock_guard<std::mutex> guard(in_flight_mutex_);
  default_handler_ = std::move(cb);
}

nl_sock* NlClient::Sock() const { return sock_.get(); }

}  // namespace avd

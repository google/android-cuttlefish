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
#include "host/commands/wifi_relay/cmd.h"

namespace cvd {

Cmd::Cmd() : msg_(nlmsg_alloc()) {}

Cmd::Cmd(nlmsghdr* h) : msg_(nlmsg_convert(h)) {}

Cmd::Cmd(nl_msg* h) {
  nlmsg_get(h);
  msg_ = h;
}

Cmd::~Cmd() {
  for (auto& msg : responses_) {
    nlmsg_free(msg);
  }
  nlmsg_free(msg_);
}

bool Cmd::OnResponse(nl_msg* msg) {
  // nlmsg_get increases refcount on msg, but does not return the msg
  // so we can't exactly use it as an argument to unique_ptr.
  nlmsg_get(msg);
  responses_.emplace_back(msg);
  auto hdr = nlmsg_hdr(msg);

  // Kernel documentation seems to be a bit misleading on this topic saying:
  //
  //     In multipart messages (multiple nlmsghdr headers with associated
  //     payload in one byte stream) the first and all following headers have
  //     the NLM_F_MULTI flag set, except for the last header which has the type
  //     NLMSG_DONE.
  //
  // In theory, that would make processing multi-part messages simple, but in
  // practice this does not seem to be true. Specifying exit criteria solely on
  // NLM_F_MULTI flag setting will block some, if not all calls that dump
  // NL80211 wifi interfaces for example.
  if (!(hdr->nlmsg_flags & NLM_F_MULTI) || (hdr->nlmsg_type == NLMSG_DONE) ||
      (hdr->nlmsg_type == NLMSG_ERROR)) {
    std::lock_guard<std::mutex> lock(ready_mutex_);
    ready_signal_.notify_all();
    return true;
  }

  return false;
}

const std::vector<nl_msg*> Cmd::Responses() const {
  WaitComplete();
  return responses_;
}

void Cmd::WaitComplete() const {
  std::unique_lock<std::mutex> lock(ready_mutex_);
  ready_signal_.wait(lock, [this]() { return responses_.size() > 0; });
}

}  // namespace cvd

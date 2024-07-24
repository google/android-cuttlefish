/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "proxy_common.h"

#include <sys/socket.h>

#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>

#include <cstdlib>
#include <string>
#include "absl/status/status.h"

namespace cuttlefish::process_sandboxer {

absl::StatusOr<Message> Message::RecvFrom(int sock) {
  msghdr empty_hdr;
  int len = recvmsg(sock, &empty_hdr, MSG_PEEK | MSG_TRUNC);
  if (len < 0) {
    return absl::ErrnoToStatus(errno, "recvmsg with MSG_PEEK failed");
  }

  Message message;
  message.data_ = std::string(len, '\0');

  iovec msg_iovec = iovec{
      .iov_base = reinterpret_cast<void*>(message.data_.data()),
      .iov_len = static_cast<size_t>(len),
  };

  union {
    char buf[CMSG_SPACE(sizeof(ucred))];
    struct cmsghdr align;
  } cmsg_data;
  std::memset(cmsg_data.buf, 0, sizeof(cmsg_data.buf));

  msghdr hdr = msghdr{
      .msg_iov = &msg_iovec,
      .msg_iovlen = 1,
      .msg_control = cmsg_data.buf,
      .msg_controllen = sizeof(cmsg_data.buf),
  };

  auto recvmsg_ret = recvmsg(sock, &hdr, 0);
  if (recvmsg_ret < 0) {
    return absl::ErrnoToStatus(errno, "recvmsg failed");
  }

  for (auto cmsg = CMSG_FIRSTHDR(&hdr); cmsg != nullptr;
       cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_CREDENTIALS) {
      message.credentials_ = *(ucred*)CMSG_DATA(cmsg);
    }
  }

  return message;
}

const std::string& Message::Data() const { return data_; }

const std::optional<ucred>& Message::Credentials() const {
  return credentials_;
}

absl::StatusOr<size_t> SendStringMsg(int sock, std::string_view msg) {
  iovec msg_iovec = iovec{
      .iov_base = (void*)msg.data(),
      .iov_len = msg.length(),
  };

  msghdr hdr = msghdr{
      .msg_iov = &msg_iovec,
      .msg_iovlen = 1,
  };

  auto ret = sendmsg(sock, &hdr, MSG_EOR | MSG_NOSIGNAL);
  return ret >= 0 ? absl::StatusOr<size_t>(ret)
                  : absl::ErrnoToStatus(errno, "sendmsg failed");
}

}  // namespace cuttlefish::process_sandboxer

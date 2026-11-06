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

#include "allocd/net/nfnetlink_socket.h"

#include <errno.h>
#include <linux/netlink.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <memory>
#include <utility>
#include <vector>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr size_t kReplyBufferSize = 16384;

class KernelNfnetlinkSocket : public NfnetlinkSocket {
 public:
  explicit KernelNfnetlinkSocket(SharedFD fd) : fd_(std::move(fd)) {}

  Result<std::vector<char>> Transact(
      const std::vector<char>& messages) override {
    const ssize_t sent = fd_->Send(messages.data(), messages.size(), 0);
    CF_EXPECTF(sent == static_cast<ssize_t>(messages.size()),
               "Failed to send to netfilter: {}", fd_->StrError());
    std::vector<char> replies;
    std::vector<char> buffer(kReplyBufferSize);
    // The kernel handles the messages before send() returns, so every reply
    // is already queued.
    for (;;) {
      const ssize_t received =
          fd_->Recv(buffer.data(), buffer.size(), MSG_DONTWAIT);
      if (received >= 0) {
        replies.insert(replies.end(), buffer.data(), buffer.data() + received);
        continue;
      }
      const int error = fd_->GetErrno();
      if (error == EAGAIN || error == EWOULDBLOCK) {
        break;
      }
      return CF_ERRF("Failed to receive from netfilter: {}", fd_->StrError());
    }
    return replies;
  }

 private:
  SharedFD fd_;
};

}  // namespace

Result<std::unique_ptr<NfnetlinkSocket>> NfnetlinkSocket::Open() {
  SharedFD fd =
      SharedFD::Socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_NETFILTER);
  CF_EXPECTF(fd->IsOpen(), "Failed to open a netfilter socket: {}",
             fd->StrError());
  sockaddr_nl address{};
  address.nl_family = AF_NETLINK;
  CF_EXPECT(fd->Bind(reinterpret_cast<sockaddr*>(&address), sizeof(address)),
            "Failed to bind the netfilter socket");
  return std::unique_ptr<NfnetlinkSocket>(
      new KernelNfnetlinkSocket(std::move(fd)));
}

}  // namespace cuttlefish

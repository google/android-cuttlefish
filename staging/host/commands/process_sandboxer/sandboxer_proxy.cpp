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

#include <sys/socket.h>
#include <sys/un.h>

#include <iostream>

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include "absl/strings/numbers.h"
#include "proxy_common.h"

namespace cuttlefish::process_sandboxer {
namespace {

template <typename T>
T UnwrapStatusOr(absl::StatusOr<T> status_or) {
  if (!status_or.ok()) {
    std::cerr << status_or.status().ToString() << '\n';
    abort();
  }
  return std::move(*status_or);
}

template <typename T>
absl::StatusOr<T> AtoiOr(std::string_view str) {
  T out;
  if (!absl::SimpleAtoi(str, &out)) {
    return absl::InvalidArgumentError("Not an integer");
  }
  return out;
}

absl::StatusOr<int> OpenSandboxManagerSocket() {
  int sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (sock < 0) {
    return absl::ErrnoToStatus(errno, "`socket` failed");
  }

  sockaddr_un addr = sockaddr_un{
      .sun_family = AF_UNIX,
  };
  size_t size = std::min(sizeof(addr.sun_path), kManagerSocketPath.size());
  strncpy(addr.sun_path, kManagerSocketPath.data(), size);

  if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
    return absl::ErrnoToStatus(errno, "`connect` failed");
  }

  return sock;
}

int ProcessSandboxerMain() {
  int sock = UnwrapStatusOr(OpenSandboxManagerSocket());
  UnwrapStatusOr(SendStringMsg(sock, kHandshakeBegin));
  UnwrapStatusOr(SendStringMsg(sock, std::to_string(sock)));
  Message pingback = UnwrapStatusOr(Message::RecvFrom(sock));
  UnwrapStatusOr(SendStringMsg(sock, pingback.Data()));

  // If signals other than SIGKILL become relevant, this should `poll` to check
  // both `sock` and a `signalfd`.
  while (true) {
    Message command = UnwrapStatusOr(Message::RecvFrom(sock));
    if (command.Data() == "exit") {
      Message message = UnwrapStatusOr(Message::RecvFrom(sock));
      return UnwrapStatusOr(AtoiOr<int>(message.Data()));
    }
    std::cerr << "Unexpected message: '" << command.Data() << "'\n";
    return 1;
  }

  return 0;
}

}  // namespace
}  // namespace cuttlefish::process_sandboxer

int main() { return cuttlefish::process_sandboxer::ProcessSandboxerMain(); }

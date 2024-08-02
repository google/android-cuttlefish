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
#include "host/commands/process_sandboxer/credentialed_unix_server.h"

#include <sys/socket.h>
#include <sys/un.h>

#include <cstring>
#include <string>

#include <absl/status/statusor.h>

#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish::process_sandboxer {

CredentialedUnixServer::CredentialedUnixServer(UniqueFd fd)
    : fd_(std::move(fd)) {}

absl::StatusOr<CredentialedUnixServer> CredentialedUnixServer::Open(
    const std::string& path) {
  UniqueFd fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));

  if (fd.Get() < 0) {
    return absl::ErrnoToStatus(errno, "`socket` failed");
  }
  sockaddr_un socket_name = {
      .sun_family = AF_UNIX,
  };
  std::snprintf(socket_name.sun_path, sizeof(socket_name.sun_path), "%s",
                path.c_str());
  sockaddr* sockname_ptr = reinterpret_cast<sockaddr*>(&socket_name);
  if (bind(fd.Get(), sockname_ptr, sizeof(socket_name)) < 0) {
    return absl::ErrnoToStatus(errno, "`bind` failed");
  }

  int enable_passcred = 1;
  if (setsockopt(fd.Get(), SOL_SOCKET, SO_PASSCRED, &enable_passcred,
                 sizeof(enable_passcred)) < 0) {
    static constexpr char kErr[] = "`setsockopt(..., SO_PASSCRED, ...)` failed";
    return absl::ErrnoToStatus(errno, kErr);
  }

  if (listen(fd.Get(), 10) < 0) {
    return absl::ErrnoToStatus(errno, "`listen` failed");
  }

  return CredentialedUnixServer(std::move(fd));
}

absl::StatusOr<UniqueFd> CredentialedUnixServer::AcceptClient() {
  UniqueFd client(accept4(fd_.Get(), nullptr, nullptr, SOCK_CLOEXEC));
  if (client.Get() < 0) {
    return absl::ErrnoToStatus(errno, "`accept` failed");
  }
  return client;
}

int CredentialedUnixServer::Fd() const { return fd_.Get(); }

}  // namespace cuttlefish::process_sandboxer

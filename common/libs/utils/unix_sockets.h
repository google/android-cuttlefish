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
#pragma once

#include <sys/socket.h>

#include <cstdint>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

struct ControlMessage {
 public:
  static ControlMessage FromRaw(const cmsghdr*);
  static Result<ControlMessage> FromFileDescriptors(
      const std::vector<SharedFD>&);
#ifdef __linux__
  static ControlMessage FromCredentials(const ucred&);
#endif
  ControlMessage(const ControlMessage&) = delete;
  ControlMessage(ControlMessage&&);
  ~ControlMessage();
  ControlMessage& operator=(const ControlMessage&) = delete;
  ControlMessage& operator=(ControlMessage&&);

  const cmsghdr* Raw() const;

#ifdef __linux__
  bool IsCredentials() const;
  Result<ucred> AsCredentials() const;
#endif

  bool IsFileDescriptors() const;
  Result<std::vector<SharedFD>> AsSharedFDs() const;

 private:
  friend class UnixMessageSocket;
  ControlMessage() = default;
  cmsghdr* Raw();

  std::vector<char> data_;
  std::vector<int> fds_;
};

struct UnixSocketMessage {
  std::vector<char> data;
  std::vector<ControlMessage> control;

  bool HasFileDescriptors();
  Result<std::vector<SharedFD>> FileDescriptors();
#ifdef __linux__
  bool HasCredentials();
  Result<ucred> Credentials();
#endif
};

class UnixMessageSocket {
 public:
  UnixMessageSocket(SharedFD);
  [[nodiscard]] Result<void> WriteMessage(const UnixSocketMessage&);
  Result<UnixSocketMessage> ReadMessage();

#ifdef __linux__
  [[nodiscard]] Result<void> EnableCredentials(bool);
#endif

 private:
  SharedFD socket_;
  std::uint32_t max_message_size_;
};

}  // namespace cuttlefish

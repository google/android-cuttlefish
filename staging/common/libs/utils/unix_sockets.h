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

#include <android-base/result.h>

#include <cstdint>
#include <variant>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class UnixMessageSocket;

struct ControlMessage {
 public:
  static ControlMessage FromRaw(const cmsghdr*);
  static android::base::Result<ControlMessage> FromFileDescriptors(
      const std::vector<SharedFD>&);
  static ControlMessage FromCredentials(const ucred&);
  ControlMessage(const ControlMessage&) = delete;
  ControlMessage(ControlMessage&&);
  ~ControlMessage();
  ControlMessage& operator=(const ControlMessage&) = delete;
  ControlMessage& operator=(ControlMessage&&);

  const cmsghdr* Raw() const;

  bool IsCredentials() const;
  android::base::Result<ucred> AsCredentials() const;

  bool IsFileDescriptors() const;
  android::base::Result<std::vector<SharedFD>> AsSharedFDs() const;

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
  android::base::Result<std::vector<SharedFD>> FileDescriptors();
  bool HasCredentials();
  android::base::Result<ucred> Credentials();
};

class UnixMessageSocket {
 public:
  UnixMessageSocket(SharedFD);
  [[nodiscard]] android::base::Result<void> WriteMessage(
      const UnixSocketMessage&);
  android::base::Result<UnixSocketMessage> ReadMessage();

  [[nodiscard]] android::base::Result<void> EnableCredentials(bool);

 private:
  SharedFD socket_;
  std::uint32_t max_message_size_;
};

}  // namespace cuttlefish

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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_SANDBOX_PROCESS_PROXY_COMMON_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_SANDBOX_PROCESS_PROXY_COMMON_H

#include <sys/socket.h>
#include <sys/un.h>

#include "absl/status/statusor.h"

#include <optional>
#include <string>
#include <string_view>

namespace cuttlefish {
namespace process_sandboxer {

static const constexpr std::string_view kHandshakeBegin = "hello";
static const constexpr std::string_view kManagerSocketPath = "/manager.sock";

class Message {
 public:
  static absl::StatusOr<Message> RecvFrom(int sock);

  const std::string& Data() const;
  absl::StatusOr<int> DataAsInt() const;

  const std::optional<ucred>& Credentials() const;

  std::string StrError() const;

 private:
  Message() = default;

  std::string data_;
  std::optional<ucred> credentials_;
};

absl::StatusOr<size_t> SendStringMsg(int sock, std::string_view msg);

}  // namespace process_sandboxer
}  // namespace cuttlefish
#endif

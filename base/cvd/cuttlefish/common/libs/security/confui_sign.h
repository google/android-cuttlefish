/*
 * Copyright 2022 The Android Open Source Project
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

#include <optional>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace confui {
enum class SignMessageError : std::uint8_t {
  kOk = 0,
  kUnknownError = 1,
};

struct SignRawMessage {
  SignMessageError error_;
  std::vector<std::uint8_t> payload_;
};
}  // end of namespace confui

class ConfUiSignerImpl {
  // status and its mask bits
  using ReceiveError = std::uint8_t;
  static const ReceiveError kIoError = 1;
  static const ReceiveError kLogicError = 2;

 public:
  ConfUiSignerImpl() : sign_status_(0) {}

  bool IsIoError() const { return (kIoError & sign_status_) != 0; }

  bool IsLogicError() const { return (kLogicError & sign_status_) != 0; }

  bool IsOk() const { return !IsIoError() && !IsLogicError(); }

  // set the sign_status_ if there was an error
  // TODO(kwstephenkim@): use android::base::Result. aosp/1940753
  bool Send(SharedFD output, const confui::SignMessageError error,
            const std::vector<std::uint8_t>& payload);
  std::optional<confui::SignRawMessage> Receive(SharedFD input);

 private:
  ReceiveError sign_status_;
};

/*
 * secure_env will use this in order:
 *
 *   Receive() // receive request
 *   Send() // send signature
 */
class ConfUiSignSender {
  using SignMessageError = confui::SignMessageError;

 public:
  ConfUiSignSender(SharedFD fd) : server_fd_(fd) {}

  // note that the error is IO error
  std::optional<confui::SignRawMessage> Receive();
  bool Send(const SignMessageError error,
            const std::vector<std::uint8_t>& encoded_hmac);

  bool IsOk() const { return impl_.IsOk(); }
  bool IsIoError() const { return impl_.IsIoError(); }
  bool IsLogicError() const { return impl_.IsLogicError(); }

 private:
  SharedFD server_fd_;
  ConfUiSignerImpl impl_;
};

/**
 * confirmation UI host will use this in this order:
 *
 *   Request()
 *   Receive()
 */
class ConfUiSignRequester {
  using SignMessageError = confui::SignMessageError;

 public:
  ConfUiSignRequester(SharedFD fd) : client_fd_(fd) {}
  bool Request(const std::vector<std::uint8_t>& message);
  std::optional<confui::SignRawMessage> Receive();

 private:
  SharedFD client_fd_;
  ConfUiSignerImpl impl_;
};
}  // end of namespace cuttlefish

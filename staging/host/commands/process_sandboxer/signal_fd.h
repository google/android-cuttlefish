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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_SIGNAL_FD_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_SIGNAL_FD_H

#include <sys/signalfd.h>

#include <absl/status/statusor.h>

#include "host/commands/process_sandboxer/unique_fd.h"

namespace cuttlefish::process_sandboxer {

class SignalFd {
 public:
  static absl::StatusOr<SignalFd> AllExceptSigChld();

  absl::StatusOr<signalfd_siginfo> ReadSignal();

  int Fd() const;

 private:
  SignalFd(UniqueFd);

  UniqueFd fd_;
};

}  // namespace cuttlefish::process_sandboxer

#endif

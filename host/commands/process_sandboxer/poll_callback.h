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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_POLL_CALLBACK_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_POLL_CALLBACK_H

#include <poll.h>

#include <functional>
#include <vector>

#include <absl/status/status.h>

namespace cuttlefish {
namespace process_sandboxer {

class PollCallback {
 public:
  void Add(int fd, std::function<absl::Status(short)> cb);

  absl::Status Poll();

 private:
  std::vector<pollfd> pollfds_;
  std::vector<std::function<absl::Status(short)>> callbacks_;
};

}  // namespace process_sandboxer
}  // namespace cuttlefish

#endif

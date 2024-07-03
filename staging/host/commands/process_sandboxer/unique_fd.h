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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_UNIQUE_FD_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_PROCESS_SANDBOXER_UNIQUE_FD_H

namespace cuttlefish {
namespace process_sandboxer {

class UniqueFd {
 public:
  UniqueFd() = default;
  explicit UniqueFd(int fd);
  UniqueFd(UniqueFd&&);
  UniqueFd(UniqueFd&) = delete;
  ~UniqueFd();
  UniqueFd& operator=(UniqueFd&&);

  int Get() const;
  int Release();
  void Reset(int fd);

 private:
  void Close();

  int fd_ = -1;
};

}  // namespace process_sandboxer
}  // namespace cuttlefish

#endif

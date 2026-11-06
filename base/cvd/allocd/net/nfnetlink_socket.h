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

#ifndef ALLOCD_NET_NFNETLINK_SOCKET_H_
#define ALLOCD_NET_NFNETLINK_SOCKET_H_

#include <memory>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

class NfnetlinkSocket {
 public:
  static Result<std::unique_ptr<NfnetlinkSocket>> Open();

  virtual ~NfnetlinkSocket() = default;

  // Sends the messages and returns all of the kernel's replies at once.
  virtual Result<std::vector<char>> Transact(
      const std::vector<char>& messages) = 0;
};

}  // namespace cuttlefish

#endif  // ALLOCD_NET_NFNETLINK_SOCKET_H_

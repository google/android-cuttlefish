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

#include "allocd/test/fake_nfnetlink_socket.h"

#include <memory>
#include <utility>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

FakeNfnetlinkSocket::FakeNfnetlinkSocket(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

Result<std::vector<char>> FakeNfnetlinkSocket::Transact(
    const std::vector<char>& messages) {
  state_->sent.push_back(messages);
  if (state_->replies.empty()) {
    return std::vector<char>{};
  }
  std::vector<char> reply = std::move(state_->replies.front());
  state_->replies.pop_front();
  return reply;
}

}  // namespace cuttlefish

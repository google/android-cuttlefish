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

#ifndef ALLOCD_TEST_FAKE_NFNETLINK_SOCKET_H_
#define ALLOCD_TEST_FAKE_NFNETLINK_SOCKET_H_

#include <deque>
#include <memory>
#include <vector>

#include "allocd/net/nfnetlink_socket.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Records what is sent and returns queued replies. The state is shared so a
// test can still inspect it after the socket is destroyed.
class FakeNfnetlinkSocket : public NfnetlinkSocket {
 public:
  struct State {
    // Everything passed to Transact, in order.
    std::vector<std::vector<char>> sent;
    // Replies for the next Transact calls, then empty replies.
    std::deque<std::vector<char>> replies;
  };

  explicit FakeNfnetlinkSocket(std::shared_ptr<State> state);

  Result<std::vector<char>> Transact(
      const std::vector<char>& messages) override;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace cuttlefish

#endif  // ALLOCD_TEST_FAKE_NFNETLINK_SOCKET_H_

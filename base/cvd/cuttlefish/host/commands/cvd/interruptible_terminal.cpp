/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/interruptible_terminal.h"

#include <errno.h>

#include <android-base/scopeguard.h>

#include "common/libs/fs/shared_select.h"

namespace cuttlefish {

InterruptibleTerminal::InterruptibleTerminal(SharedFD stdin_fd)
    : stdin_fd_(std::move(stdin_fd)), interrupt_event_fd_(SharedFD::Event()) {}

Result<void> InterruptibleTerminal::Interrupt() {
  std::unique_lock lock(terminal_mutex_);
  interrupted_ = true;
  if (owner_tid_) {
    CF_EXPECT_EQ(interrupt_event_fd_->EventfdWrite(1), 0,
                 interrupt_event_fd_->StrError());
  }
  readline_done_.wait(lock, [this]() { return owner_tid_ == std::nullopt; });
  { SharedFD discard = std::move(stdin_fd_); }
  return {};
}

// only up to one thread can call this function
Result<std::string> InterruptibleTerminal::ReadLine() {
  {
    std::lock_guard lock(terminal_mutex_);
    CF_EXPECT(interrupted_ == false, "Interrupted");
    CF_EXPECTF(owner_tid_ == std::nullopt,
               "This InterruptibleTerminal is already owned by {}",
               owner_tid_.value());
    CF_EXPECT(stdin_fd_->IsOpen(),
              "The copy of client stdin fd has been already closed.");
    owner_tid_ = std::this_thread::get_id();
  }
  SharedFDSet read_set;
  std::string line_buf;
  while (true) {
    read_set.Set(interrupt_event_fd_);
    read_set.Set(stdin_fd_);
    int num_fds = Select(&read_set, nullptr, nullptr, nullptr);

    std::lock_guard lock(terminal_mutex_);
    auto return_action = android::base::ScopeGuard([this]() {
      owner_tid_ = std::nullopt;
      readline_done_.notify_one();
    });
    CF_EXPECT(interrupted_ == false, "Interrupted");
    CF_EXPECTF(num_fds >= 0,
               "Select call to read the user input returned error: {}",
               strerror(errno));

    if (read_set.IsSet(interrupt_event_fd_)) {
      eventfd_t val;
      CF_EXPECT_EQ(interrupt_event_fd_->EventfdRead(&val), 0);
      return CF_ERR("Terminal input interrupted.");
    }
    CF_EXPECT(read_set.IsSet(stdin_fd_));
    char c = 0;
    const char end_of_transmission = 4;
    auto n_read = stdin_fd_->Read(&c, sizeof(c));
    CF_EXPECT(n_read >= 0,
              "Read from stdin returned an error: " << stdin_fd_->StrError());
    if (n_read > 0) {
      CF_EXPECTF(n_read == 1, "Expected to read 1 byte but read: {} bytes",
                 n_read);
      if (c == '\n' || c == 0 || c == end_of_transmission) {
        return line_buf;
      }
      line_buf.append(1, c);
      continue;
    }
    return line_buf;
  }
}

}  // namespace cuttlefish

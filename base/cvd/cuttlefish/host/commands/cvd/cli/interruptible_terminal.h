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

#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

class InterruptibleTerminal {
 public:
  InterruptibleTerminal();
  /*
   * Returns a line from the stdin_fd, which is the client stdin
   *
   * Notes:
   *  1. Only up to one thread can call this function, so each handler
   *     should have its own copy
   *  2. Each handler release the interrupt_lock before calling
   *     ReadLine(), get the interrupt lock again afterwards, and
   *     check the interrupted_ flag
   *
   */
  Result<std::string> ReadLine();

 private:
  SharedFD interrupt_event_fd_;
  bool interrupted_ = false;
  // one owner per InterruptibleTerminal
  // also protecting interrupted_
  std::mutex terminal_mutex_;
  std::optional<std::thread::id> owner_tid_;
  std::condition_variable readline_done_;
};

}  // namespace cuttlefish

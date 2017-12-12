/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "common/vsoc/shm/lock.h"

#include "sys/types.h"

#include "common/vsoc/lib/compat.h"
#include "common/vsoc/lib/single_sided_signal.h"

namespace vsoc {
namespace layout {

void HostLock::Lock() {
  uint32_t tid = gettid();
  uint32_t expected_value;
  uint32_t* uaddr = reinterpret_cast<uint32_t*>(&lock_uint32_);

  while (1) {
    if (TryLock(tid, &expected_value)) {
      return;
    }
    SingleSidedSignal::AwaitSignal(expected_value, uaddr);
  }
}

void HostLock::Unlock() {
  Sides sides_to_signal = UnlockCommon(gettid());
  if (sides_to_signal.value_ != Sides::NoSides) {
    SingleSidedSignal::Signal(&lock_uint32_);
  }
}

}  // namespace layout
}  // namespace vsoc

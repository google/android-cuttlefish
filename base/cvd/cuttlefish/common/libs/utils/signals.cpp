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

#include "common/libs/utils/signals.h"

#include <errno.h>
#include <signal.h>
#include <string.h>

#include <android-base/logging.h>

namespace cuttlefish {

SignalMasker::SignalMasker(sigset_t signals) {
  auto res = sigprocmask(SIG_SETMASK, &signals, &old_mask_);
  auto err = errno;
  CHECK(res == 0) << "Failed to set thread's blocked signal mask: "
                  << strerror(err);
}

SignalMasker::~SignalMasker() {
  auto res = sigprocmask(SIG_SETMASK, &old_mask_, NULL);
  auto err = errno;
  CHECK(res == 0) << "Failed to reset thread's blocked signal mask: "
                  << strerror(err);
}

}  // namespace cuttlefish


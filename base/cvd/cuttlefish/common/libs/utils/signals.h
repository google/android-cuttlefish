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

#pragma once

#include <signal.h>

#include <vector>

namespace cuttlefish {

/**
 * Blocks signals for the current thread for the lifetime of the object.
 *
 * Provides a RAII interface to sigprocmask.
 */
class SignalMasker {
 public:
  /**
   * Blocks the given signals until the object is destroyed.
   */
  SignalMasker(sigset_t signals);
  SignalMasker(const SignalMasker&) = delete;
  SignalMasker(SignalMasker&&) = delete;
  SignalMasker operator=(const SignalMasker&) = delete;
  SignalMasker operator=(SignalMasker&&) = delete;
  ~SignalMasker();

 private:
  sigset_t old_mask_;
};

void ChangeSignalHandlers(void(*handler)(int), std::vector<int> signals);

}  // namespace cuttlefish

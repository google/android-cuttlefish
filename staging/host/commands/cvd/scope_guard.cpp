/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include "host/commands/cvd/scope_guard.h"

#include <functional>

namespace cuttlefish {

ScopeGuard::ScopeGuard() = default;

ScopeGuard::ScopeGuard(std::function<void()> fn) : fn_(fn) {}

ScopeGuard::ScopeGuard(ScopeGuard&&) = default;

ScopeGuard& ScopeGuard::operator=(ScopeGuard&&) = default;

ScopeGuard::~ScopeGuard() {
  if (fn_) {
    fn_();
  }
}

void ScopeGuard::Cancel() { fn_ = nullptr; }

}  // namespace cuttlefish

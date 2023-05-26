/*
 * Copyright 2023 The Android Open Source Project
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
 *
 */

#pragma once

#include <fruit/fruit.h>

#include "host/commands/secure_env/oemlock.h"

namespace cuttlefish {
namespace oemlock {

class SoftOemLock : public OemLock {
 public:
  INJECT(SoftOemLock()) : is_allowed_by_carrier_(true), is_allowed_by_device_(false) {}

  bool IsOemUnlockAllowedByCarrier() const override {
    return is_allowed_by_carrier_;
  }

  bool IsOemUnlockAllowedByDevice() const override {
    return is_allowed_by_device_;
  }

  void SetOemUnlockAllowedByCarrier(bool allowed) override {
    is_allowed_by_carrier_ = allowed;
  }

  void SetOemUnlockAllowedByDevice(bool allowed) override {
    is_allowed_by_device_ = allowed;
  }

 private:
  bool is_allowed_by_carrier_;
  bool is_allowed_by_device_;
};

} // namespace oemlock
} // namespace cuttlefish
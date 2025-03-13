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

#include <fmt/core.h>

namespace cuttlefish {
namespace pci {

class Address {
 public:
  Address(unsigned int bus, unsigned int device, unsigned int function);

  unsigned int Bus() const { return bus_; };
  unsigned int Device() const { return device_; }
  unsigned int Function() const { return function_; }
  std::string Id() const {
    return fmt::format("{:02x}:{:02x}.{:01x}", bus_, device_, function_);
  }

 private:
  unsigned int bus_;
  unsigned int device_;
  unsigned int function_;
};

}  // namespace pci
}  // namespace cuttlefish

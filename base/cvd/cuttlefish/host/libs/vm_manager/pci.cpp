//
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/libs/vm_manager/pci.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace pci {
namespace {
constexpr unsigned int kMaxBus = 255;
constexpr unsigned int kMaxDevice = 31;
constexpr unsigned int kMaxFunction = 7;
}  // namespace

Address::Address(unsigned int bus, unsigned int device, unsigned int function)
    : bus_(bus), device_(device), function_(function) {
  if (bus_ > kMaxBus || device_ > kMaxDevice || function_ > kMaxFunction) {
    LOG(FATAL) << "Failed to create PCI address instance with bus: " << bus_
               << " device: " << device_ << " function: " << function_;
  }
}

}  // namespace pci
}  // namespace cuttlefish
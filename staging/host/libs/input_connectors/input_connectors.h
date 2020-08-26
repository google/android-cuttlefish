//
// Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <cinttypes>
#include <memory>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class KeyboardConnector {
 public:
  static std::unique_ptr<KeyboardConnector> Create(
      SharedFD server, bool use_virtio_events = false);

  virtual ~KeyboardConnector();

  virtual void InjectKeyEvent(uint16_t code, bool down) = 0;

 protected:
  KeyboardConnector();
};

class TouchConnector {
 public:
  static std::unique_ptr<TouchConnector> Create(SharedFD server,
                                                bool use_virtio_events = false);

  virtual ~TouchConnector();

  virtual void InjectTouchEvent(int32_t x, int32_t y, bool down) = 0;

 protected:
  TouchConnector();
};

}  // namespace cuttlefish
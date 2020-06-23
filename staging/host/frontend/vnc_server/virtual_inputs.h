#pragma once

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

#include "vnc_utils.h"

#include <map>
#include <mutex>

namespace cuttlefish {
namespace vnc {

class VirtualInputs {
 public:
  static VirtualInputs* Get();

  virtual ~VirtualInputs() = default;

  virtual void GenerateKeyPressEvent(int code, bool down) = 0;
  virtual void PressPowerButton(bool down) = 0;
  virtual void HandlePointerEvent(bool touch_down, int x, int y) = 0;

 protected:
  VirtualInputs();

  std::map<uint32_t, uint16_t> keymapping_;
};

}  // namespace vnc
}  // namespace cuttlefish

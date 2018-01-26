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

#include "common/vsoc/lib/input_events_region_view.h"

namespace cvd {
namespace vnc {

class VirtualInputs {
 public:
  VirtualInputs();

  void GenerateKeyPressEvent(int code, bool down);
  void PressPowerButton(bool down);
  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  std::shared_ptr<vsoc::input_events::InputEventsRegionView>
      input_events_region_view_;
  std::map<uint32_t, uint32_t> keymapping_;
};

}  // namespace vnc
}  // namespace cvd

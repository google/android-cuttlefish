/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0f
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>

namespace cuttlefish {
namespace confui {
enum class ConfUiKeys : std::uint32_t { Confirm = 7, Cancel = 8 };

/**
 * webrtc will deliver the user inputs from their client
 * to this class object
 */
class HostVirtualInput {
 public:
  virtual void TouchEvent(const int x, const int y, const bool is_down) = 0;
  virtual void UserAbortEvent() = 0;
  virtual ~HostVirtualInput() = default;
  // guarantees that if this returns true, it is confirmation UI mode
  virtual bool IsConfUiActive() = 0;
};
}  // namespace confui
}  // namespace cuttlefish

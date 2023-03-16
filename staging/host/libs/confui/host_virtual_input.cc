/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/confui/host_virtual_input.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace confui {

HostVirtualInput::HostVirtualInput(HostServer& host_server,
                                   HostModeCtrl& host_mode_ctrl)
    : host_server_(host_server), host_mode_ctrl_(host_mode_ctrl) {}

void HostVirtualInput::TouchEvent(const int x, const int y,
                                  const bool is_down) {
  std::string mode("Android Mode");
  if (IsConfUiActive()) {
    mode = std::string("Confirmation UI Mode");
  }
  if (is_down) {
    ConfUiLog(INFO) << "TouchEvent occurs in " << mode << " at [" << x << ", "
                    << y << "]";
  }
  host_server_.TouchEvent(x, y, is_down);
}

void HostVirtualInput::UserAbortEvent() { host_server_.UserAbortEvent(); }

bool HostVirtualInput::IsConfUiActive() {
  return host_mode_ctrl_.IsConfirmatioUiMode();
}

}  // namespace confui
}  // namespace cuttlefish

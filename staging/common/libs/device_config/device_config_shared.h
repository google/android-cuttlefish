/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "device/google/cuttlefish/common/libs/device_config/device_config.pb.h"

namespace cuttlefish {

// Minimal version to share with Goldfish which doesn't include additional
// Cuttlefish headers.
//
// TODO(natsu): switch ranchu hwcomposer to drm for display discovery.
DeviceConfig GetDeviceConfig();

}  // namespace cuttlefish
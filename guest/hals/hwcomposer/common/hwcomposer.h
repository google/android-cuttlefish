/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <memory>

#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>

#include "guest/hals/hwcomposer/common/screen_view.h"

#define IS_TARGET_FRAMEBUFFER(x) ((x) == HWC_FRAMEBUFFER_TARGET)
#define IS_PRIMARY_DISPLAY(x) ((x) == HWC_DISPLAY_PRIMARY)
#define IS_EXTERNAL_DISPLAY(x) ((x) == HWC_DISPLAY_EXTERNAL)

namespace cuttlefish {
int cvd_hwc_open(std::unique_ptr<ScreenView> screen_view,
                 const struct hw_module_t* module, const char* name,
                 struct hw_device_t** device);
}  // namespace cuttlefish

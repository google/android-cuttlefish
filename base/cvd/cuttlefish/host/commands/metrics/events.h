/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <stdint.h>

#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "external_proto/cf_log.pb.h"

namespace cuttlefish::metrics {

logs::proto::wireless::android::cuttlefish::CuttlefishLogEvent BuildCfLogEvent(
    uint64_t now_ms);

int SendVMStart(VmmMode);
int SendVMStop(VmmMode);
int SendDeviceBoot(VmmMode);
int SendLockScreen(VmmMode);

}  // namespace cuttlefish

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
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>

#include "guest/monitoring/dumpstate_ext/dumpstate_device.h"

using ::android::OK;
using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::dumpstate::V1_1::IDumpstateDevice;
using ::android::hardware::dumpstate::V1_1::implementation::DumpstateDevice;
using ::android::hardware::joinRpcThreadpool;
using ::android::sp;

int main() {
  sp<IDumpstateDevice> dumpstate = new DumpstateDevice;
  // This method MUST be called before interacting with any HIDL interfaces.
  configureRpcThreadpool(1, true);
  if (dumpstate->registerAsService() != OK) {
    ALOGE("Could not register service.");
    return 1;
  }
  joinRpcThreadpool();
}

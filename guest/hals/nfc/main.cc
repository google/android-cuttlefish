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

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Nfc.h"
using ::aidl::android::hardware::nfc::Nfc;

int main() {
  LOG(INFO) << "NFC HAL starting up";
  if (!ABinderProcess_setThreadPoolMaxThreadCount(1)) {
    LOG(INFO) << "failed to set thread pool max thread count";
    return 1;
  }
  std::shared_ptr<Nfc> nfc_service = ndk::SharedRefBase::make<Nfc>();

  const std::string instance = std::string() + Nfc::descriptor + "/default";
  CHECK_EQ(STATUS_OK, AServiceManager_addService(nfc_service->asBinder().get(),
                                                 instance.c_str()));
  ABinderProcess_joinThreadPool();
  return 0;
}

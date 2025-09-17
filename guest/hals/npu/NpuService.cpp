// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "SchedulingService.h"

using aidl::android::hardware::npu::SchedulingService;

int main() {
  auto service = ndk::SharedRefBase::make<SchedulingService>();
  const std::string name =
      std::string() + SchedulingService::descriptor + "/default";
  binder_status_t status =
      AServiceManager_addService(service->asBinder().get(), name.c_str());
  if (status != STATUS_OK) {
    return EXIT_FAILURE;
  }
  ABinderProcess_joinThreadPool();
  return EXIT_FAILURE;
}
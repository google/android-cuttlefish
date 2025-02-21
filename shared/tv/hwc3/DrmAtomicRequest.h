/*
 * Copyright 2022 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Common.h"
#include "DrmMode.h"
#include "DrmProperty.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmAtomicRequest {
 public:
  static std::unique_ptr<DrmAtomicRequest> create();
  ~DrmAtomicRequest();

  bool Set(uint32_t objectId, const DrmProperty& prop, uint64_t value);

  bool Commit(::android::base::borrowed_fd drmFd);

 private:
  DrmAtomicRequest(drmModeAtomicReqPtr request) : mRequest(request) {}

  drmModeAtomicReqPtr mRequest;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

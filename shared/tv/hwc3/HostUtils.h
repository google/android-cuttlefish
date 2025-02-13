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

#ifndef ANDROID_HWC_HOSTUTILS_H
#define ANDROID_HWC_HOSTUTILS_H

#include "Common.h"
#include "HostConnection.h"

namespace aidl::android::hardware::graphics::composer3::impl {

HostConnection* createOrGetHostConnection();

inline HWC3::Error getAndValidateHostConnection(
    HostConnection** ppHostCon, ExtendedRCEncoderContext** ppRcEnc) {
  *ppHostCon = nullptr;
  *ppRcEnc = nullptr;

  HostConnection* hostCon = createOrGetHostConnection();
  if (!hostCon) {
    ALOGE("%s: Failed to get host connection\n", __FUNCTION__);
    return HWC3::Error::NoResources;
  }
  ExtendedRCEncoderContext* rcEnc = hostCon->rcEncoder();
  if (!rcEnc) {
    ALOGE("%s: Failed to get renderControl encoder context\n", __FUNCTION__);
    return HWC3::Error::NoResources;
  }

  *ppHostCon = hostCon;
  *ppRcEnc = rcEnc;
  return HWC3::Error::None;
}

#define DEFINE_AND_VALIDATE_HOST_CONNECTION                           \
  HostConnection* hostCon;                                            \
  ExtendedRCEncoderContext* rcEnc;                                    \
  {                                                                   \
    HWC3::Error res = getAndValidateHostConnection(&hostCon, &rcEnc); \
    if (res != HWC3::Error::None) {                                   \
      return res;                                                     \
    }                                                                 \
  }
}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif

/*
 * Copyright (C) 2026 The Android Open Source Project
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

// Queries NVENC codec support from the GPU at startup and caches the
// result. The temporary encode session is opened once via std::call_once
// and the supported codec GUIDs are stored for the lifetime of the
// process.

#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_capabilities.h"

#include <cstring>
#include <mutex>
#include <vector>

#include <nvEncodeAPI.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/cuda_context.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/cuda_loader.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_loader.h"
#include "rtc_base/logging.h"

namespace cuttlefish {
namespace {

const std::vector<GUID>& QuerySupportedCodecGuids() {
  static std::once_flag flag;
  static std::vector<GUID> guids;

  std::call_once(flag, [] {
    const auto* funcs = TryLoadNvenc();
    if (funcs == nullptr) {
      RTC_LOG(LS_WARNING) << "NVENC capabilities: "
                          << "NVENC not available";
      return;
    }

    auto context = CudaContext::Get(0);
    if (!context) {
      RTC_LOG(LS_WARNING) << "NVENC capabilities: "
                          << "failed to get CUDA context for device 0";
      return;
    }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_params =
        {NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER};
    open_params.device = context->get();
    open_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open_params.apiVersion = NVENCAPI_VERSION;

    void* encoder = nullptr;
    NVENCSTATUS status =
        funcs->nvEncOpenEncodeSessionEx(&open_params, &encoder);
    if (status != NV_ENC_SUCCESS) {
      RTC_LOG(LS_WARNING) << "NVENC capabilities: "
                          << "nvEncOpenEncodeSessionEx failed: "
                          << status;
      return;
    }

    uint32_t guid_count = 0;
    funcs->nvEncGetEncodeGUIDCount(encoder, &guid_count);
    guids.resize(guid_count);
    funcs->nvEncGetEncodeGUIDs(encoder, guids.data(), guid_count,
                               &guid_count);
    funcs->nvEncDestroyEncoder(encoder);

    RTC_LOG(LS_INFO) << "NVENC capabilities: " << guids.size()
                     << " codec GUID(s) supported by GPU";
  });

  return guids;
}

}  // namespace

bool IsNvencCodecSupported(GUID codec_guid) {
  const auto& guids = QuerySupportedCodecGuids();
  for (const auto& guid : guids) {
    if (memcmp(&guid, &codec_guid, sizeof(GUID)) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace cuttlefish

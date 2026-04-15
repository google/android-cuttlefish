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

#include "cuttlefish/host/libs/gpu/nvenc_capabilities.h"

#include <memory>
#include <string.h>
#include <vector>

#include <nvEncodeAPI.h>

#include "absl/log/log.h"

#include "cuttlefish/host/libs/gpu/cuda_context.h"
#include "cuttlefish/host/libs/gpu/cuda_loader.h"
#include "cuttlefish/host/libs/gpu/nvenc_loader.h"

namespace cuttlefish {
namespace {

std::vector<GUID> LoadSupportedCodecGuids(int device_id) {
  Result<const NV_ENCODE_API_FUNCTION_LIST*> funcs_result =
      TryLoadNvenc();
  if (!funcs_result.ok()) {
    LOG(WARNING) << "NVENC capabilities: NVENC not available";
    return {};
  }
  const NV_ENCODE_API_FUNCTION_LIST* funcs = *funcs_result;

  std::shared_ptr<CudaContext> context =
      CudaContext::Get(device_id);
  if (!context) {
    LOG(WARNING) << "NVENC capabilities: "
                 << "failed to get CUDA context for device "
                 << device_id;
    return {};
  }

  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_params =
      {NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER};
  open_params.device = context->get();
  open_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  open_params.apiVersion = NVENCAPI_VERSION;

  void* raw_encoder = nullptr;
  NVENCSTATUS status =
      funcs->nvEncOpenEncodeSessionEx(&open_params, &raw_encoder);
  if (status != NV_ENC_SUCCESS) {
    LOG(WARNING) << "NVENC capabilities: "
                 << "nvEncOpenEncodeSessionEx failed: "
                 << status;
    return {};
  }

  auto deleter = [funcs](void* e) {
    funcs->nvEncDestroyEncoder(e);
  };
  std::unique_ptr<void, decltype(deleter)> encoder(
      raw_encoder, deleter);

  uint32_t guid_count = 0;
  status = funcs->nvEncGetEncodeGUIDCount(
      encoder.get(), &guid_count);
  if (status != NV_ENC_SUCCESS) {
    LOG(WARNING) << "NVENC capabilities: "
                 << "nvEncGetEncodeGUIDCount failed: "
                 << status;
    return {};
  }

  std::vector<GUID> guids(guid_count);
  status = funcs->nvEncGetEncodeGUIDs(
      encoder.get(), guids.data(), guid_count, &guid_count);
  if (status != NV_ENC_SUCCESS) {
    LOG(WARNING) << "NVENC capabilities: "
                 << "nvEncGetEncodeGUIDs failed: " << status;
    return {};
  }

  LOG(INFO) << "NVENC capabilities: " << guids.size()
            << " codec GUID(s) supported by GPU";
  return guids;
}

}  // namespace

bool IsNvencCodecSupported(GUID codec_guid, int device_id) {
  static std::vector<GUID> guids = LoadSupportedCodecGuids(device_id);
  for (const GUID& guid : guids) {
    if (memcmp(&guid, &codec_guid, sizeof(GUID)) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace cuttlefish

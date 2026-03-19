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

#include "cuttlefish/host/frontend/webrtc/libcommon/cuda_context.h"

#include "rtc_base/logging.h"

namespace cuttlefish {

std::shared_ptr<CudaContext> CudaContext::Get(int device_id) {
  static std::mutex mutex;
  static std::map<int, std::weak_ptr<CudaContext>> instances;

  RTC_LOG(LS_INFO) << "CudaContext::Get(device_id=" << device_id << ") called";

  std::lock_guard<std::mutex> lock(mutex);

  // Check if we already have a context for this device
  auto it = instances.find(device_id);
  if (it != instances.end()) {
    auto shared = it->second.lock();
    if (shared) {
      RTC_LOG(LS_INFO) << "Reusing existing CUDA context for device " << device_id;
      return shared;
    }
    // Weak pointer expired, remove stale entry
    RTC_LOG(LS_INFO) << "Previous context expired, creating new one for device " << device_id;
    instances.erase(it);
  }

  // Initialize CUDA if needed
  // Note: cuInit can be called multiple times safely
  RTC_LOG(LS_INFO) << "Initializing CUDA driver (cuInit)...";
  CUresult res = cuInit(0);
  if (res != CUDA_SUCCESS) {
    RTC_LOG(LS_ERROR) << "cuInit failed with error code: " << static_cast<int>(res);
    return nullptr;
  }
  RTC_LOG(LS_INFO) << "cuInit succeeded";

  CUdevice device;
  res = cuDeviceGet(&device, device_id);
  if (res != CUDA_SUCCESS) {
    RTC_LOG(LS_ERROR) << "cuDeviceGet failed for device " << device_id
                      << " with error code: " << static_cast<int>(res);
    return nullptr;
  }
  RTC_LOG(LS_INFO) << "cuDeviceGet succeeded for device " << device_id;

  // Log device name for debugging
  char device_name[256];
  if (cuDeviceGetName(device_name, sizeof(device_name), device) == CUDA_SUCCESS) {
    RTC_LOG(LS_INFO) << "CUDA Device " << device_id << ": " << device_name;
  }

  // Use Primary Context for sharing across threads
  RTC_LOG(LS_INFO) << "Retaining primary context for device " << device_id << "...";
  CUcontext ctx;
  res = cuDevicePrimaryCtxRetain(&ctx, device);
  if (res != CUDA_SUCCESS) {
    RTC_LOG(LS_ERROR) << "cuDevicePrimaryCtxRetain failed for device "
                      << device_id << " with error code: " << static_cast<int>(res);
    return nullptr;
  }
  RTC_LOG(LS_INFO) << "cuDevicePrimaryCtxRetain succeeded, context=" << ctx;

  auto shared = std::shared_ptr<CudaContext>(new CudaContext(ctx, device_id));
  instances[device_id] = shared;

  RTC_LOG(LS_INFO) << "Created and cached CUDA context for device " << device_id;
  return shared;
}

CudaContext::CudaContext(CUcontext ctx, int device_id) 
    : ctx_(ctx), device_id_(device_id) {}

CudaContext::~CudaContext() {
  if (ctx_) {
    CUdevice device;
    CUresult res = cuDeviceGet(&device, device_id_);
    if (res == CUDA_SUCCESS) {
      cuDevicePrimaryCtxRelease(device);
    }
    // If cuDeviceGet failed, the device is already gone - nothing to release
  }
}

ScopedCudaContext::ScopedCudaContext(CUcontext ctx) : push_succeeded_(false) {
  if (ctx == nullptr) {
    RTC_LOG(LS_ERROR) << "ScopedCudaContext: null context provided";
    return;
  }

  CUresult res = cuCtxPushCurrent(ctx);
  if (res != CUDA_SUCCESS) {
    RTC_LOG(LS_ERROR) << "cuCtxPushCurrent failed with error: " << res;
    return;
  }

  push_succeeded_ = true;
}

ScopedCudaContext::~ScopedCudaContext() {
  if (push_succeeded_) {
    CUcontext popped_ctx;
    CUresult res = cuCtxPopCurrent(&popped_ctx);
    if (res != CUDA_SUCCESS) {
      RTC_LOG(LS_ERROR) << "cuCtxPopCurrent failed with error: " << res;
    }
  }
}

}  // namespace cuttlefish

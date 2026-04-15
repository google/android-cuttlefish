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

#include "cuttlefish/host/libs/gpu/cuda_context.h"

#include "absl/log/log.h"

#include "cuttlefish/host/libs/gpu/cuda_loader.h"

namespace cuttlefish {

std::shared_ptr<CudaContext> CudaContext::Get(int device_id) {
  static std::mutex mutex;
  static std::map<int, std::weak_ptr<CudaContext>> instances;

  Result<const CudaFunctions*> cuda_result = TryLoadCuda();
  if (!cuda_result.ok()) {
    return nullptr;
  }
  const CudaFunctions* cuda = *cuda_result;

  std::lock_guard<std::mutex> lock(mutex);

  auto it = instances.find(device_id);
  if (it != instances.end()) {
    std::shared_ptr<CudaContext> shared = it->second.lock();
    if (shared) {
      return shared;
    }
    instances.erase(it);
  }

  CUresult res = cuda->cuInit(0);
  if (res != CUDA_SUCCESS) {
    LOG(WARNING) << "cuInit failed: " << static_cast<int>(res);
    return nullptr;
  }

  CUdevice device;
  res = cuda->cuDeviceGet(&device, device_id);
  if (res != CUDA_SUCCESS) {
    LOG(WARNING) << "cuDeviceGet failed for device " << device_id
                 << ": " << static_cast<int>(res);
    return nullptr;
  }

  char device_name[256];
  device_name[0] = '\0';
  cuda->cuDeviceGetName(device_name, sizeof(device_name), device);

  CUcontext ctx;
  res = cuda->cuDevicePrimaryCtxRetain(&ctx, device);
  if (res != CUDA_SUCCESS) {
    LOG(WARNING) << "cuDevicePrimaryCtxRetain failed for device "
                 << device_id << ": " << static_cast<int>(res);
    return nullptr;
  }

  std::shared_ptr<CudaContext> shared(
      new CudaContext(ctx, device_id, cuda));
  instances[device_id] = shared;

  LOG(INFO) << "CUDA context created for device " << device_id
            << " (" << device_name << ")";
  return shared;
}

Result<ScopedCudaContext> CudaContext::Acquire(int device_id) {
  std::shared_ptr<CudaContext> ctx = Get(device_id);
  CF_EXPECT(ctx != nullptr,
            "Failed to get CUDA context for device " << device_id);
  ScopedCudaContext scope(ctx->get(), ctx->cuda_);
  CF_EXPECT(scope.ok(), "Failed to push CUDA context");
  return scope;
}

CudaContext::CudaContext(CUcontext ctx, int device_id,
                         const CudaFunctions* cuda)
    : ctx_(ctx), device_id_(device_id), cuda_(cuda) {}

CudaContext::~CudaContext() {
  if (ctx_ && cuda_) {
    CUdevice device;
    CUresult res = cuda_->cuDeviceGet(&device, device_id_);
    if (res == CUDA_SUCCESS) {
      cuda_->cuDevicePrimaryCtxRelease(device);
    }
  }
}

ScopedCudaContext::ScopedCudaContext(CUcontext ctx,
                                     const CudaFunctions* cuda)
    : cuda_(cuda), push_succeeded_(false) {
  if (ctx == nullptr || cuda == nullptr) {
    LOG(ERROR) << "ScopedCudaContext: null context or CUDA functions";
    return;
  }

  CUresult res = cuda_->cuCtxPushCurrent(ctx);
  if (res != CUDA_SUCCESS) {
    LOG(ERROR) << "cuCtxPushCurrent failed: " << res;
    return;
  }

  push_succeeded_ = true;
}

ScopedCudaContext::ScopedCudaContext(ScopedCudaContext&& other)
    : cuda_(other.cuda_), push_succeeded_(other.push_succeeded_) {
  other.push_succeeded_ = false;
}

ScopedCudaContext::~ScopedCudaContext() {
  if (push_succeeded_ && cuda_) {
    CUcontext popped_ctx;
    CUresult res = cuda_->cuCtxPopCurrent(&popped_ctx);
    if (res != CUDA_SUCCESS) {
      LOG(ERROR) << "cuCtxPopCurrent failed: " << res;
    }
  }
}

}  // namespace cuttlefish

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

#include "cuttlefish/host/frontend/webrtc/libcommon/cuda_loader.h"

#include <dlfcn.h>

#include <mutex>

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<void> LoadSymbol(void* lib, const char* name,
                        void** out) {
  *out = dlsym(lib, name);
  CF_EXPECT_NE(*out, nullptr,
               "Failed to load symbol: " << name);
  return {};
}

Result<CudaFunctions> LoadCuda() {
  void* lib = dlopen("libcuda.so.1", RTLD_LAZY);
  CF_EXPECT_NE(lib, nullptr,
               "libcuda.so.1 not available: " << dlerror());

  CudaFunctions f{};

  // Context management (versioned symbols per CUDA ABI)
  CF_EXPECT(LoadSymbol(lib, "cuInit",
      reinterpret_cast<void**>(&f.cuInit)));
  CF_EXPECT(LoadSymbol(lib, "cuDeviceGet",
      reinterpret_cast<void**>(&f.cuDeviceGet)));
  CF_EXPECT(LoadSymbol(lib, "cuDeviceGetName",
      reinterpret_cast<void**>(&f.cuDeviceGetName)));
  CF_EXPECT(LoadSymbol(lib, "cuDevicePrimaryCtxRetain",
      reinterpret_cast<void**>(
          &f.cuDevicePrimaryCtxRetain)));
  CF_EXPECT(LoadSymbol(lib, "cuDevicePrimaryCtxRelease",
      reinterpret_cast<void**>(
          &f.cuDevicePrimaryCtxRelease)));
  CF_EXPECT(LoadSymbol(lib, "cuCtxPushCurrent_v2",
      reinterpret_cast<void**>(&f.cuCtxPushCurrent)));
  CF_EXPECT(LoadSymbol(lib, "cuCtxPopCurrent_v2",
      reinterpret_cast<void**>(&f.cuCtxPopCurrent)));

  // Stream management
  CF_EXPECT(LoadSymbol(lib, "cuStreamCreate",
      reinterpret_cast<void**>(&f.cuStreamCreate)));
  CF_EXPECT(LoadSymbol(lib, "cuStreamDestroy_v2",
      reinterpret_cast<void**>(&f.cuStreamDestroy)));
  CF_EXPECT(LoadSymbol(lib, "cuStreamSynchronize",
      reinterpret_cast<void**>(&f.cuStreamSynchronize)));

  // Memory management
  CF_EXPECT(LoadSymbol(lib, "cuMemAllocPitch_v2",
      reinterpret_cast<void**>(&f.cuMemAllocPitch)));
  CF_EXPECT(LoadSymbol(lib, "cuMemFree_v2",
      reinterpret_cast<void**>(&f.cuMemFree)));
  CF_EXPECT(LoadSymbol(lib, "cuMemcpy2DAsync_v2",
      reinterpret_cast<void**>(&f.cuMemcpy2DAsync)));

  // Error handling
  CF_EXPECT(LoadSymbol(lib, "cuGetErrorString",
      reinterpret_cast<void**>(&f.cuGetErrorString)));

  return f;
}

}  // namespace

const CudaFunctions* TryLoadCuda() {
  static std::once_flag flag;
  static Result<CudaFunctions>* cached = nullptr;

  std::call_once(flag, [] {
    cached = new Result<CudaFunctions>(LoadCuda());
  });

  if (cached->ok()) {
    return &(cached->value());
  }
  return nullptr;
}

}  // namespace cuttlefish

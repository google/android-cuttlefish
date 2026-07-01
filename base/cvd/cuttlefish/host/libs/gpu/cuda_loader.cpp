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

#include "cuttlefish/host/libs/gpu/cuda_loader.h"

#include <dlfcn.h>

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

// Owns the dlopen handle. Intentionally never calls dlclose — the CUDA
// driver segfaults on unload after GPU operations have occurred.
struct CudaLibrary {
  void* handle = nullptr;
  CudaFunctions funcs{};
};

Result<CudaLibrary> LoadCuda() {
  CudaLibrary lib;
  lib.handle = dlopen("libcuda.so.1", RTLD_LAZY);
  CF_EXPECT_NE(lib.handle, nullptr,
               "libcuda.so.1 not available: " << dlerror());

  CudaFunctions& f = lib.funcs;

  CF_EXPECT(LoadSymbol(lib.handle, "cuInit",
      reinterpret_cast<void**>(&f.cuInit)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuDeviceGet",
      reinterpret_cast<void**>(&f.cuDeviceGet)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuDeviceGetName",
      reinterpret_cast<void**>(&f.cuDeviceGetName)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuDevicePrimaryCtxRetain",
      reinterpret_cast<void**>(
          &f.cuDevicePrimaryCtxRetain)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuDevicePrimaryCtxRelease",
      reinterpret_cast<void**>(
          &f.cuDevicePrimaryCtxRelease)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuCtxPushCurrent_v2",
      reinterpret_cast<void**>(&f.cuCtxPushCurrent)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuCtxPopCurrent_v2",
      reinterpret_cast<void**>(&f.cuCtxPopCurrent)));

  CF_EXPECT(LoadSymbol(lib.handle, "cuStreamCreate",
      reinterpret_cast<void**>(&f.cuStreamCreate)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuStreamDestroy_v2",
      reinterpret_cast<void**>(&f.cuStreamDestroy)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuStreamSynchronize",
      reinterpret_cast<void**>(&f.cuStreamSynchronize)));

  CF_EXPECT(LoadSymbol(lib.handle, "cuMemAllocPitch_v2",
      reinterpret_cast<void**>(&f.cuMemAllocPitch)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuMemFree_v2",
      reinterpret_cast<void**>(&f.cuMemFree)));
  CF_EXPECT(LoadSymbol(lib.handle, "cuMemcpy2DAsync_v2",
      reinterpret_cast<void**>(&f.cuMemcpy2DAsync)));

  CF_EXPECT(LoadSymbol(lib.handle, "cuGetErrorString",
      reinterpret_cast<void**>(&f.cuGetErrorString)));

  return lib;
}

}  // namespace

Result<const CudaFunctions*> TryLoadCuda() {
  static Result<CudaLibrary> cached = LoadCuda();
  if (!cached.ok()) {
    return CF_ERR(cached.error().Message());
  }
  return &(cached.value().funcs);
}

}  // namespace cuttlefish

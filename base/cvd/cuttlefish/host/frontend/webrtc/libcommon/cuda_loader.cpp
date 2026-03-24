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

#include "rtc_base/logging.h"

namespace cuttlefish {
namespace {

bool LoadSymbol(void* lib, const char* name, void** out) {
  *out = dlsym(lib, name);
  if (*out == nullptr) {
    RTC_LOG(LS_WARNING) << "CUDA loader: cannot load " << name;
    return false;
  }
  return true;
}

}  // namespace

const CudaFunctions* TryLoadCuda() {
  static std::once_flag flag;
  static const CudaFunctions* result = nullptr;

  std::call_once(flag, [] {
    void* lib = dlopen("libcuda.so.1", RTLD_LAZY);
    if (lib == nullptr) {
      RTC_LOG(LS_INFO) << "CUDA loader: libcuda.so.1 not available"
                       << " (" << dlerror() << ")";
      return;
    }
    RTC_LOG(LS_INFO) << "CUDA loader: loaded libcuda.so.1";

    auto* f = new CudaFunctions{};
    bool ok = true;

    // Context management (versioned symbols where required by ABI)
    ok &= LoadSymbol(lib, "cuInit",
                     reinterpret_cast<void**>(&f->cuInit));
    ok &= LoadSymbol(lib, "cuDeviceGet",
                     reinterpret_cast<void**>(&f->cuDeviceGet));
    ok &= LoadSymbol(lib, "cuDeviceGetName",
                     reinterpret_cast<void**>(&f->cuDeviceGetName));
    ok &= LoadSymbol(lib, "cuDevicePrimaryCtxRetain",
                     reinterpret_cast<void**>(
                         &f->cuDevicePrimaryCtxRetain));
    ok &= LoadSymbol(lib, "cuDevicePrimaryCtxRelease",
                     reinterpret_cast<void**>(
                         &f->cuDevicePrimaryCtxRelease));
    ok &= LoadSymbol(lib, "cuCtxPushCurrent_v2",
                     reinterpret_cast<void**>(&f->cuCtxPushCurrent));
    ok &= LoadSymbol(lib, "cuCtxPopCurrent_v2",
                     reinterpret_cast<void**>(&f->cuCtxPopCurrent));

    // Stream management
    ok &= LoadSymbol(lib, "cuStreamCreate",
                     reinterpret_cast<void**>(&f->cuStreamCreate));
    ok &= LoadSymbol(lib, "cuStreamDestroy_v2",
                     reinterpret_cast<void**>(&f->cuStreamDestroy));
    ok &= LoadSymbol(lib, "cuStreamSynchronize",
                     reinterpret_cast<void**>(
                         &f->cuStreamSynchronize));

    // Memory management
    ok &= LoadSymbol(lib, "cuMemAllocPitch_v2",
                     reinterpret_cast<void**>(&f->cuMemAllocPitch));
    ok &= LoadSymbol(lib, "cuMemFree_v2",
                     reinterpret_cast<void**>(&f->cuMemFree));
    ok &= LoadSymbol(lib, "cuMemcpy2DAsync_v2",
                     reinterpret_cast<void**>(&f->cuMemcpy2DAsync));

    // Error handling
    ok &= LoadSymbol(lib, "cuGetErrorString",
                     reinterpret_cast<void**>(&f->cuGetErrorString));

    if (!ok) {
      RTC_LOG(LS_WARNING) << "CUDA loader: failed to resolve all "
                          << "symbols, CUDA unavailable";
      delete f;
      return;
    }

    RTC_LOG(LS_INFO) << "CUDA loader: all 14 symbols resolved";
    result = f;
  });

  return result;
}

}  // namespace cuttlefish

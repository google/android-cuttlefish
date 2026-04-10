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

#include "cuttlefish/host/libs/gpu/nvenc_loader.h"

#include <dlfcn.h>

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Owns the dlopen handle. Never calls dlclose (same as CudaLibrary).
struct NvencLibrary {
  void* handle = nullptr;
  NV_ENCODE_API_FUNCTION_LIST funcs{};
};

Result<NvencLibrary> LoadNvenc() {
  NvencLibrary lib;
  lib.handle = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
  CF_EXPECT_NE(lib.handle, nullptr,
               "libnvidia-encode.so.1 not available: "
                   << dlerror());

  typedef NVENCSTATUS(NVENCAPI * NvEncodeAPICreateInstanceFn)(
      NV_ENCODE_API_FUNCTION_LIST*);
  NvEncodeAPICreateInstanceFn fn =
      reinterpret_cast<NvEncodeAPICreateInstanceFn>(
          dlsym(lib.handle, "NvEncodeAPICreateInstance"));
  CF_EXPECT_NE(fn, nullptr,
               "Cannot load NvEncodeAPICreateInstance");

  lib.funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  NVENCSTATUS status = fn(&lib.funcs);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "NvEncodeAPICreateInstance failed: " << status);

  return lib;
}

}  // namespace

Result<const NV_ENCODE_API_FUNCTION_LIST*> TryLoadNvenc() {
  static Result<NvencLibrary> cached = LoadNvenc();
  if (!cached.ok()) {
    return CF_ERR(cached.error().Message());
  }
  return &(cached.value().funcs);
}

}  // namespace cuttlefish

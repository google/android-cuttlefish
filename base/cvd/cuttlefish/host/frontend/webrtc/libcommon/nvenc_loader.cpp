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

#include "cuttlefish/host/frontend/webrtc/libcommon/nvenc_loader.h"

#include <dlfcn.h>

#include <mutex>

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<NV_ENCODE_API_FUNCTION_LIST> LoadNvenc() {
  void* lib = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
  CF_EXPECT_NE(lib, nullptr,
               "libnvidia-encode.so.1 not available: "
                   << dlerror());

  auto fn = reinterpret_cast<NVENCSTATUS (*)(
      NV_ENCODE_API_FUNCTION_LIST*)>(
      dlsym(lib, "NvEncodeAPICreateInstance"));
  CF_EXPECT_NE(fn, nullptr,
               "Cannot load NvEncodeAPICreateInstance");

  NV_ENCODE_API_FUNCTION_LIST funcs{};
  funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  NVENCSTATUS status = fn(&funcs);
  CF_EXPECT(status == NV_ENC_SUCCESS,
            "NvEncodeAPICreateInstance failed: " << status);

  return funcs;
}

}  // namespace

const NV_ENCODE_API_FUNCTION_LIST* TryLoadNvenc() {
  static std::once_flag flag;
  static Result<NV_ENCODE_API_FUNCTION_LIST>* cached =
      nullptr;

  std::call_once(flag, [] {
    cached = new Result<NV_ENCODE_API_FUNCTION_LIST>(
        LoadNvenc());
  });

  if (cached->ok()) {
    return &(cached->value());
  }
  return nullptr;
}

}  // namespace cuttlefish

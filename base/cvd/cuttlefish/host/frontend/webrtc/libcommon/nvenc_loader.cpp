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

#include "rtc_base/logging.h"

namespace cuttlefish {

const NvencFunctions* TryLoadNvenc() {
  static std::once_flag flag;
  static const NvencFunctions* result = nullptr;

  std::call_once(flag, [] {
    void* lib = dlopen("libnvidia-encode.so.1", RTLD_LAZY);
    if (lib == nullptr) {
      RTC_LOG(LS_INFO) << "NVENC loader: libnvidia-encode.so.1 not "
                       << "available (" << dlerror() << ")";
      return;
    }
    RTC_LOG(LS_INFO) << "NVENC loader: loaded libnvidia-encode.so.1";

    auto fn = reinterpret_cast<NVENCSTATUS (*)(
        NV_ENCODE_API_FUNCTION_LIST*)>(
        dlsym(lib, "NvEncodeAPICreateInstance"));
    if (fn == nullptr) {
      RTC_LOG(LS_WARNING) << "NVENC loader: cannot load "
                          << "NvEncodeAPICreateInstance";
      return;
    }

    auto* f = new NvencFunctions{};
    f->NvEncodeAPICreateInstance = fn;

    RTC_LOG(LS_INFO) << "NVENC loader: NvEncodeAPICreateInstance "
                     << "resolved";
    result = f;
  });

  return result;
}

}  // namespace cuttlefish

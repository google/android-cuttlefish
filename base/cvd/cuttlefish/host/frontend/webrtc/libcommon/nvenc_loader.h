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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_LOADER_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_LOADER_H_

#include <nvEncodeAPI.h>

namespace cuttlefish {

// Attempts to load libnvidia-encode.so.1, resolve
// NvEncodeAPICreateInstance, and populate the NVENC function table.
//
// Returns a pointer to a static NV_ENCODE_API_FUNCTION_LIST on
// success, or nullptr if the library is not available or
// NvEncodeAPICreateInstance fails. Thread-safe (loads once via
// std::call_once). The dlopen handle and function list are
// intentionally leaked.
const NV_ENCODE_API_FUNCTION_LIST* TryLoadNvenc();

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_LOADER_H_

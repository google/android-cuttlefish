// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_CAPABILITIES_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_CAPABILITIES_H_

#include <nvEncodeAPI.h>

namespace cuttlefish {

// Returns true if the GPU at device 0 supports encoding with the
// given NVENC codec GUID. Opens a temporary encode session on first
// call, queries all supported codec GUIDs, and caches the result.
// Subsequent calls use the cache. Thread-safe.
bool IsNvencCodecSupported(GUID codec_guid);

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_NVENC_CAPABILITIES_H_

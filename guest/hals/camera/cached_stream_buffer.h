/*
 * Copyright (C) 2021 The Android Open Source Project
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
#pragma once
#include <android/hardware/camera/device/3.4/ICameraDeviceSession.h>
#include "HandleImporter.h"

namespace android::hardware::camera::device::V3_4::implementation {

using ::android::hardware::camera::common::V1_0::helper::HandleImporter;
using ::android::hardware::camera::device::V3_2::StreamBuffer;

// Small wrapper for allocating/freeing native handles
class ReleaseFence {
 public:
  ReleaseFence(int fence_fd);
  ~ReleaseFence();

  native_handle_t* handle() const { return handle_; }

 private:
  native_handle_t* handle_;
};

// CachedStreamBuffer holds a buffer of camera3 stream.
class CachedStreamBuffer {
 public:
  CachedStreamBuffer();
  CachedStreamBuffer(const StreamBuffer& buffer);
  // Not copyable
  CachedStreamBuffer(const CachedStreamBuffer&) = delete;
  CachedStreamBuffer& operator=(const CachedStreamBuffer&) = delete;
  // ...but movable
  CachedStreamBuffer(CachedStreamBuffer&& from) noexcept;
  CachedStreamBuffer& operator=(CachedStreamBuffer&& from) noexcept;

  ~CachedStreamBuffer();

  bool valid() const { return buffer_ != nullptr; }
  uint64_t bufferId() const { return buffer_id_; }
  int32_t streamId() const { return stream_id_; }
  int acquireFence() const { return acquire_fence_; }

  void importFence(const native_handle_t* fence_handle);
  // Acquire methods wait first on acquire fence and then return pointers to
  // data. Data is nullptr if the wait timed out
  YCbCrLayout acquireAsYUV(int32_t width, int32_t height, int timeout_ms);
  void* acquireAsBlob(int32_t size, int timeout_ms);
  int release();

 private:
  buffer_handle_t buffer_;
  uint64_t buffer_id_;
  int32_t stream_id_;
  int acquire_fence_;
};

}  // namespace android::hardware::camera::device::V3_4::implementation

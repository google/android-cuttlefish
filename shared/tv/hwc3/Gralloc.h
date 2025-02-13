/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC_GRALLOC_H
#define ANDROID_HWC_GRALLOC_H

#include <aidl/android/hardware/graphics/common/PlaneLayout.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>
#include <utils/StrongPointer.h>

#include <memory>
#include <optional>
#include <vector>

namespace aidl::android::hardware::graphics::composer3::impl {

class Gralloc;
class GrallocBuffer;

// An RAII object that will Unlock() a GrallocBuffer upon destruction.
class GrallocBufferView {
 public:
  virtual ~GrallocBufferView();

  GrallocBufferView(const GrallocBufferView& rhs) = delete;
  GrallocBufferView& operator=(const GrallocBufferView& rhs) = delete;

  GrallocBufferView(GrallocBufferView&& rhs);
  GrallocBufferView& operator=(GrallocBufferView&& rhs);

  const std::optional<void*> Get() const;

  const std::optional<android_ycbcr>& GetYCbCr() const;

 private:
  friend class GrallocBuffer;
  GrallocBufferView(GrallocBuffer* buffer, void* raw);
  GrallocBufferView(GrallocBuffer* buffer, android_ycbcr raw);

  // The GrallocBuffer that should be unlocked upon destruction of this object.
  GrallocBuffer* gralloc_buffer_ = nullptr;

  std::optional<void*> locked_;
  std::optional<android_ycbcr> locked_ycbcr_;
};

// A gralloc 4.0 buffer that has been imported in the current process and
// that will be released upon destruction. Users must ensure that the Gralloc
// instance that this buffer is created with out lives this buffer.
class GrallocBuffer {
 public:
  GrallocBuffer(Gralloc* gralloc, buffer_handle_t buffer);
  virtual ~GrallocBuffer();

  GrallocBuffer(const GrallocBuffer& rhs) = delete;
  GrallocBuffer& operator=(const GrallocBuffer& rhs) = delete;

  GrallocBuffer(GrallocBuffer&& rhs);
  GrallocBuffer& operator=(GrallocBuffer&& rhs);

  // Locks the buffer for reading and returns a view if successful.
  std::optional<GrallocBufferView> Lock();

  std::optional<uint32_t> GetWidth();
  std::optional<uint32_t> GetHeight();
  std::optional<uint32_t> GetDrmFormat();

  // Returns the stride of the buffer if it is a single plane buffer or fails
  // and returns nullopt if the buffer is for a multi plane buffer.
  std::optional<uint32_t> GetMonoPlanarStrideBytes();

  std::optional<
      std::vector<aidl::android::hardware::graphics::common::PlaneLayout>>
  GetPlaneLayouts();

 private:
  // Internal visibility for Unlock().
  friend class GrallocBufferView;

  // Unlocks the buffer from reading.
  void Unlock();

  void Release();

  Gralloc* gralloc_ = nullptr;
  buffer_handle_t buffer_ = nullptr;
};

class Gralloc {
 public:
  virtual ~Gralloc() = default;

  // Imports the given buffer handle into the current process and returns an
  // imported buffer which can be used for reading. Users must ensure that the
  // Gralloc instance outlives any GrallocBuffers.
  std::optional<GrallocBuffer> Import(buffer_handle_t buffer);

 private:
  // The below functions are made available only to GrallocBuffer so that
  // users only call gralloc functions on *imported* buffers.
  friend class GrallocBuffer;

  // See GrallocBuffer::Release.
  void Release(buffer_handle_t buffer);

  // See GrallocBuffer::Lock.
  std::optional<void*> Lock(buffer_handle_t buffer);

  // See GrallocBuffer::LockYCbCr.
  std::optional<android_ycbcr> LockYCbCr(buffer_handle_t buffer);

  // See GrallocBuffer::Unlock.
  void Unlock(buffer_handle_t buffer);

  // See GrallocBuffer::GetWidth.
  std::optional<uint32_t> GetWidth(buffer_handle_t buffer);

  // See GrallocBuffer::GetHeight.
  std::optional<uint32_t> GetHeight(buffer_handle_t buffer);

  // See GrallocBuffer::GetDrmFormat.
  std::optional<uint32_t> GetDrmFormat(buffer_handle_t buffer);

  // See GrallocBuffer::GetPlaneLayouts.
  std::optional<
      std::vector<aidl::android::hardware::graphics::common::PlaneLayout>>
  GetPlaneLayouts(buffer_handle_t buffer);

  // Returns the stride of the buffer if it is a single plane buffer or fails
  // and returns nullopt if the buffer is for a multi plane buffer.
  std::optional<uint32_t> GetMonoPlanarStrideBytes(buffer_handle_t);
};

}  // namespace aidl::android::hardware::graphics::composer3::impl

#endif

/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <memory>
#include <optional>
#include <vector>

#include <android/hardware/graphics/mapper/3.0/IMapper.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>
#include <utils/StrongPointer.h>

namespace cuttlefish {

class Gralloc;

// A gralloc buffer that has been imported in the current process and
// that will be released upon destruction. Users must ensure that the Gralloc
// instance that this buffer is created with outlives this buffer.
class GrallocBuffer {
 public:
  GrallocBuffer(Gralloc* gralloc, buffer_handle_t buffer);
  virtual ~GrallocBuffer();

  GrallocBuffer(const GrallocBuffer& rhs) = delete;
  GrallocBuffer& operator=(const GrallocBuffer& rhs) = delete;

  GrallocBuffer(GrallocBuffer&& rhs);
  GrallocBuffer& operator=(GrallocBuffer&& rhs);

  // Locks the buffer for reading and returns the mapped address if successful.
  // Fails and returns nullopt if the underlying buffer is a YCbCr buffer.
  //
  // TODO(b/159834777): wrap lock result into a RAII object that restricts
  // usage of the mapped buffer to the lifetime of the RAII object.
  std::optional<void*> Lock();

  // Locks the buffer for reading and returns the mapped addresses and strides
  // of each plane if successful. Fails and returns nullopt if the underlying
  // buffer is not a YCbCr buffer.
  //
  // TODO(b/159834777): wrap lock result into a RAII object that restricts
  // usage of the mapped buffer to the lifetime of the RAII object.
  std::optional<android_ycbcr> LockYCbCr();

  // Unlocks the buffer from reading.
  void Unlock();

  std::optional<uint32_t> GetWidth();
  std::optional<uint32_t> GetHeight();
  std::optional<uint32_t> GetDrmFormat();

  // Returns the stride of the buffer if it is a single plane buffer or fails
  // and returns nullopt if the buffer is for a multi plane buffer.
  std::optional<uint32_t> GetMonoPlanarStrideBytes();

 private:
  void Release();

  Gralloc* gralloc_ = nullptr;
  buffer_handle_t buffer_ = nullptr;
};

class Gralloc {
 public:
  Gralloc();
  virtual ~Gralloc() = default;

  // Imports the given buffer handle into the current process and returns an
  // imported buffer which can be used for reading. Users must ensure that the
  // Gralloc instance outlives any GrallocBuffers.
  std::optional<GrallocBuffer> Import(buffer_handle_t buffer);

 private:
  // The below functions are made avaialble only to GrallocBuffer so that
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

  // Returns the stride of the buffer if it is a single plane buffer or fails
  // and returns nullopt if the buffer is for a multi plane buffer.
  std::optional<uint32_t> GetMonoPlanarStrideBytes(buffer_handle_t);

  const gralloc_module_t* gralloc0_ = nullptr;
  android::sp<android::hardware::graphics::mapper::V3_0::IMapper> gralloc3_;
};

}  // namespace cvd
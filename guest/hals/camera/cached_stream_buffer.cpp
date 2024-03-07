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
#define LOG_TAG "CachedStreamBuffer"
#include "cached_stream_buffer.h"
#include <hardware/gralloc.h>
#include <log/log.h>
#include <sync/sync.h>

namespace android::hardware::camera::device::V3_4::implementation {

namespace {
HandleImporter g_importer;
}

ReleaseFence::ReleaseFence(int fence_fd) : handle_(nullptr) {
  if (fence_fd >= 0) {
    handle_ = native_handle_create(/*numFds*/ 1, /*numInts*/ 0);
    handle_->data[0] = fence_fd;
  }
}

ReleaseFence::~ReleaseFence() {
  if (handle_ != nullptr) {
    native_handle_close(handle_);
    native_handle_delete(handle_);
  }
}

CachedStreamBuffer::CachedStreamBuffer()
    : buffer_(nullptr), buffer_id_(0), stream_id_(0), acquire_fence_(-1) {}

CachedStreamBuffer::CachedStreamBuffer(const StreamBuffer& buffer)
    : buffer_(buffer.buffer.getNativeHandle()),
      buffer_id_(buffer.bufferId),
      stream_id_(buffer.streamId),
      acquire_fence_(-1) {
  g_importer.importBuffer(buffer_);
  g_importer.importFence(buffer.acquireFence, acquire_fence_);
}

CachedStreamBuffer::CachedStreamBuffer(CachedStreamBuffer&& from) noexcept {
  buffer_ = from.buffer_;
  buffer_id_ = from.buffer_id_;
  stream_id_ = from.stream_id_;
  acquire_fence_ = from.acquire_fence_;
  from.acquire_fence_ = -1;
  from.buffer_ = nullptr;
}

CachedStreamBuffer& CachedStreamBuffer::operator=(
    CachedStreamBuffer&& from) noexcept {
  if (this != &from) {
    buffer_ = from.buffer_;
    buffer_id_ = from.buffer_id_;
    stream_id_ = from.stream_id_;
    acquire_fence_ = from.acquire_fence_;
    from.acquire_fence_ = -1;
    from.buffer_ = nullptr;
  }
  return *this;
}

CachedStreamBuffer::~CachedStreamBuffer() {
  if (buffer_ != nullptr) {
    g_importer.freeBuffer(buffer_);
  }
  g_importer.closeFence(acquire_fence_);
}

void CachedStreamBuffer::importFence(const native_handle_t* fence_handle) {
  g_importer.closeFence(acquire_fence_);
  g_importer.importFence(fence_handle, acquire_fence_);
}

YCbCrLayout CachedStreamBuffer::acquireAsYUV(int32_t width, int32_t height,
                                             int timeout_ms) {
  if (acquire_fence_ >= 0) {
    if (sync_wait(acquire_fence_, timeout_ms)) {
      ALOGW("%s: timeout while waiting acquire fence", __FUNCTION__);
      return {};
    } else {
      ::close(acquire_fence_);
      acquire_fence_ = -1;
    }
  }
  android::Rect region{0, 0, width, height};
  android_ycbcr result =
      g_importer.lockYCbCr(buffer_, GRALLOC_USAGE_SW_WRITE_OFTEN, region);
  if (result.ystride > UINT32_MAX || result.cstride > UINT32_MAX ||
      result.chroma_step > UINT32_MAX) {
    ALOGE(
        "%s: lockYCbCr failed. Unexpected values! ystride: %zu cstride: %zu "
        "chroma_step: %zu",
        __FUNCTION__, result.ystride, result.cstride, result.chroma_step);
    return {};
  }
  return {.y = result.y,
          .cb = result.cb,
          .cr = result.cr,
          .yStride = static_cast<uint32_t>(result.ystride),
          .cStride = static_cast<uint32_t>(result.cstride),
          .chromaStep = static_cast<uint32_t>(result.chroma_step)};
}

void* CachedStreamBuffer::acquireAsBlob(int32_t size, int timeout_ms) {
  if (acquire_fence_ >= 0) {
    if (sync_wait(acquire_fence_, timeout_ms)) {
      return nullptr;
    } else {
      ::close(acquire_fence_);
      acquire_fence_ = -1;
    }
  }
  return g_importer.lock(buffer_, GRALLOC_USAGE_SW_WRITE_OFTEN, size);
}

int CachedStreamBuffer::release() { return g_importer.unlock(buffer_); }

}  // namespace android::hardware::camera::device::V3_4::implementation

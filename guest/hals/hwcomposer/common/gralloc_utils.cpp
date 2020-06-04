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

#include "guest/hals/hwcomposer/common/gralloc_utils.h"

#include <drm_fourcc.h>
#include <log/log.h>

// TODO(b/146515640): remove this.
#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"
#include "guest/hals/hwcomposer/common/drm_utils.h"

// TODO(b/146515640): remove this.
using cuttlefish_gralloc0_buffer_handle_t = private_handle_t;

namespace cuttlefish {

Gralloc::Gralloc() {
  hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                reinterpret_cast<const hw_module_t**>(&gralloc0_));
  if (gralloc0_ != nullptr) {
    ALOGE("%s using Gralloc0.", __FUNCTION__);
    return;
  }
  ALOGE("%s Gralloc0 not available.", __FUNCTION__);

  ALOGE("%s No Grallocs available!", __FUNCTION__);
}

std::optional<uint32_t> Gralloc::GetWidth(buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    return gralloc0_buffer->x_res;
  }
  return std::nullopt;
}

std::optional<uint32_t> Gralloc::GetHeight(buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    return gralloc0_buffer->y_res;
  }
  return std::nullopt;
}

std::optional<uint32_t> Gralloc::GetDrmFormat(buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    return GetDrmFormatFromHalFormat(gralloc0_buffer->format);
  }
  return std::nullopt;
}

std::optional<uint32_t> Gralloc::GetMonoPlanarStrideBytes(
    buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    int bytes_per_pixel = formatToBytesPerPixel(gralloc0_buffer->format);
    return gralloc0_buffer->stride_in_pixels * bytes_per_pixel;
  }
  return std::nullopt;
}

std::optional<GrallocBuffer> Gralloc::Import(buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    return GrallocBuffer(this, buffer);
  }
  return std::nullopt;
}

void Gralloc::Release(buffer_handle_t /*buffer*/) {
  if (gralloc0_) {
    // no-opt
  }
}

std::optional<void*> Gralloc::Lock(buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    void* data = nullptr;
    int ret = gralloc0_->lock(gralloc0_,
                              gralloc0_buffer,
                              GRALLOC_USAGE_SW_READ_OFTEN,
                              0,
                              0,
                              gralloc0_buffer->x_res,
                              gralloc0_buffer->y_res,
                              &data);

    if (ret) {
      ALOGE("%s failed to lock buffer", __FUNCTION__);
      return std::nullopt;
    }
    return data;
  }
  return std::nullopt;
}

std::optional<android_ycbcr> Gralloc::LockYCbCr(buffer_handle_t buffer) {
  auto format_opt = GetDrmFormat(buffer);
  if (!format_opt) {
    ALOGE("%s failed to check format of buffer", __FUNCTION__);
    return std::nullopt;
  }

  if (*format_opt != DRM_FORMAT_NV12 &&
      *format_opt != DRM_FORMAT_NV21 &&
      *format_opt != DRM_FORMAT_YVU420) {
    ALOGE("%s called on non-ycbcr buffer", __FUNCTION__);
    return std::nullopt;
  }

  if (gralloc0_ != nullptr) {
    auto lock_opt = Lock(buffer);
    if (!lock_opt) {
      ALOGE("%s failed to lock buffer", __FUNCTION__);
      return std::nullopt;
    }
    void* data = *lock_opt;

    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    android_ycbcr buffer_ycbcr;
    formatToYcbcr(gralloc0_buffer->format,
                  gralloc0_buffer->x_res,
                  gralloc0_buffer->y_res,
                  data,
                  &buffer_ycbcr);
    return buffer_ycbcr;
  }
  return std::nullopt;
}

void Gralloc::Unlock(buffer_handle_t buffer) {
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    gralloc0_->unlock(gralloc0_, gralloc0_buffer);
    return;
  }
}


GrallocBuffer::GrallocBuffer(Gralloc* gralloc, buffer_handle_t buffer) :
  gralloc_(gralloc), buffer_(buffer) {}

GrallocBuffer::~GrallocBuffer() {  Release(); }

GrallocBuffer::GrallocBuffer(GrallocBuffer&& rhs) {
  *this = std::move(rhs);
}

GrallocBuffer& GrallocBuffer::operator=(GrallocBuffer&& rhs) {
  gralloc_ = rhs.gralloc_;
  buffer_ = rhs.buffer_;
  rhs.gralloc_ = nullptr;
  rhs.buffer_ = nullptr;
  return *this;
}

void GrallocBuffer::Release() {
  if (gralloc_ && buffer_) {
    gralloc_->Release(buffer_);
    gralloc_ = nullptr;
    buffer_ = nullptr;
  }
}

std::optional<void*> GrallocBuffer::Lock() {
  if (gralloc_ && buffer_) {
    return gralloc_->Lock(buffer_);
  }
  return std::nullopt;
}

std::optional<android_ycbcr> GrallocBuffer::LockYCbCr() {
  if (gralloc_ && buffer_) {
    return gralloc_->LockYCbCr(buffer_);
  }
  return std::nullopt;
 }

void GrallocBuffer::Unlock() {
  if (gralloc_ && buffer_) {
    gralloc_->Unlock(buffer_);
  }
}

std::optional<uint32_t> GrallocBuffer::GetWidth() {
  if (gralloc_ && buffer_) {
    return gralloc_->GetWidth(buffer_);
  }
  return std::nullopt;
}

std::optional<uint32_t> GrallocBuffer::GetHeight() {
  if (gralloc_ && buffer_) {
    return gralloc_->GetHeight(buffer_);
  }
  return std::nullopt;
}

std::optional<uint32_t> GrallocBuffer::GetDrmFormat() {
  if (gralloc_ && buffer_) {
    return gralloc_->GetDrmFormat(buffer_);
  }
  return std::nullopt;
}

std::optional<uint32_t> GrallocBuffer::GetMonoPlanarStrideBytes() {
  if (gralloc_ && buffer_) {
    return gralloc_->GetMonoPlanarStrideBytes(buffer_);
  }
  return std::nullopt;
}

}  // namespace cvd
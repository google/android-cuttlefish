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

// TODO(b/146515640): remove these after upgrading to Gralloc 4.
#include "cros_gralloc/cros_gralloc_handle.h"
#include "cros_gralloc/cros_gralloc_helpers.h"
#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"
#include "guest/hals/hwcomposer/common/drm_utils.h"

using android::hardware::graphics::common::V1_2::PixelFormat;
using android::hardware::graphics::common::V1_0::BufferUsage;
using android::hardware::hidl_handle;

// TODO(b/146515640): remove these after upgrading to Gralloc 4.
using android::hardware::graphics::mapper::V3_0::YCbCrLayout;
using cuttlefish_gralloc0_buffer_handle_t = private_handle_t;
using ErrorV3 = android::hardware::graphics::mapper::V3_0::Error;
using IMapperV3 = android::hardware::graphics::mapper::V3_0::IMapper;

namespace cuttlefish {

Gralloc::Gralloc() {
  gralloc3_ = IMapperV3::getService();
  if (gralloc3_ != nullptr) {
    ALOGE("%s using Gralloc3.", __FUNCTION__);
    return;
  }
  ALOGE("%s Gralloc3 not available.", __FUNCTION__);

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
  if (gralloc3_ != nullptr) {
    cros_gralloc_handle_t gralloc3_buffer = cros_gralloc_convert_handle(buffer);
    if (gralloc3_buffer == nullptr) {
      return std::nullopt;
    }
    return gralloc3_buffer->width;
  }
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    return gralloc0_buffer->x_res;
  }
  return std::nullopt;
}

std::optional<uint32_t> Gralloc::GetHeight(buffer_handle_t buffer) {
  if (gralloc3_ != nullptr) {
    cros_gralloc_handle_t gralloc3_buffer = cros_gralloc_convert_handle(buffer);
    if (gralloc3_buffer == nullptr) {
      return std::nullopt;
    }
    return gralloc3_buffer->height;
  }
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    return gralloc0_buffer->y_res;
  }
  return std::nullopt;
}

std::optional<uint32_t> Gralloc::GetDrmFormat(buffer_handle_t buffer) {
  if (gralloc3_ != nullptr) {
    cros_gralloc_handle_t gralloc3_buffer = cros_gralloc_convert_handle(buffer);
    if (gralloc3_buffer == nullptr) {
      return std::nullopt;
    }
    return gralloc3_buffer->format;
  }
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    return GetDrmFormatFromHalFormat(gralloc0_buffer->format);
  }
  return std::nullopt;
}

std::optional<uint32_t> Gralloc::GetMonoPlanarStrideBytes(
    buffer_handle_t buffer) {
  if (gralloc3_ != nullptr) {
    cros_gralloc_handle_t gralloc3_buffer = cros_gralloc_convert_handle(buffer);
    if (gralloc3_buffer == nullptr) {
      return std::nullopt;
    }
    return gralloc3_buffer->strides[0];
  }
  if (gralloc0_ != nullptr) {
    const cuttlefish_gralloc0_buffer_handle_t* gralloc0_buffer =
      reinterpret_cast<const cuttlefish_gralloc0_buffer_handle_t*>(buffer);

    int bytes_per_pixel = formatToBytesPerPixel(gralloc0_buffer->format);
    return gralloc0_buffer->stride_in_pixels * bytes_per_pixel;
  }
  return std::nullopt;
}

std::optional<GrallocBuffer> Gralloc::Import(buffer_handle_t buffer) {
  if (gralloc3_ != nullptr) {
    buffer_handle_t imported_buffer;

    ErrorV3 error;
    auto ret = gralloc3_->importBuffer(buffer,
                                       [&](const auto& err, const auto& buf) {
                                        error = err;
                                        if (err == ErrorV3::NONE) {
                                          imported_buffer =
                                            static_cast<buffer_handle_t>(buf);
                                        }
                                       });
    if (!ret.isOk() || error != ErrorV3::NONE) {
      ALOGE("%s failed to import buffer", __FUNCTION__);
      return std::nullopt;
    }
    return GrallocBuffer(this, imported_buffer);
  }
  if (gralloc0_ != nullptr) {
    return GrallocBuffer(this, buffer);
  }
  return std::nullopt;
}

void Gralloc::Release(buffer_handle_t buffer) {
  if (gralloc3_ != nullptr) {
    auto native_buffer = const_cast<native_handle_t*>(buffer);
    auto ret = gralloc3_->freeBuffer(native_buffer);

    if (!ret.isOk()) {
      ALOGE("%s failed to release buffer", __FUNCTION__);
    }
    return;
  }
  if (gralloc0_) {
    // no-opt
  }
}

std::optional<void*> Gralloc::Lock(buffer_handle_t buffer) {
  if (gralloc3_ != nullptr) {
    void* raw_buffer = const_cast<void*>(reinterpret_cast<const void*>(buffer));

    const auto buffer_usage = static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN);

    auto width_opt = GetWidth(buffer);
    if (!width_opt) {
      return std::nullopt;
    }

    auto height_opt = GetHeight(buffer);
    if (!height_opt) {
      return std::nullopt;
    }

    IMapperV3::Rect buffer_region;
    buffer_region.left = 0;
    buffer_region.top = 0;
    buffer_region.width = *width_opt;
    buffer_region.height = *height_opt;

    // Empty fence, lock immedietly.
    hidl_handle fence;

    ErrorV3 error = ErrorV3::NONE;
    void* data = nullptr;

    auto ret = gralloc3_->lock(raw_buffer, buffer_usage, buffer_region, fence,
                               [&](const auto& lock_error,
                                   const auto& lock_data,
                                   int32_t /*bytes_per_pixel*/,
                                   int32_t /*bytes_per_stride*/) {
                                 error = lock_error;
                                 if (lock_error == ErrorV3::NONE) {
                                  data = lock_data;
                                }
                               });

    if (!ret.isOk()) {
      error = ErrorV3::NO_RESOURCES;
    }

    if (error != ErrorV3::NONE) {
      ALOGE("%s failed to lock buffer", __FUNCTION__);
      return std::nullopt;
    }

    return data;
  }
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

  if (gralloc3_ != nullptr) {
    void* raw_buffer = const_cast<void*>(reinterpret_cast<const void*>(buffer));

    const auto buffer_usage = static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN);

    auto width_opt = GetWidth(buffer);
    if (!width_opt) {
      return std::nullopt;
    }

    auto height_opt = GetHeight(buffer);
    if (!height_opt) {
      return std::nullopt;
    }

    IMapperV3::Rect buffer_region;
    buffer_region.left = 0;
    buffer_region.top = 0;
    buffer_region.width = *width_opt;
    buffer_region.height = *height_opt;

    // Empty fence, lock immedietly.
    hidl_handle fence;

    ErrorV3 error = ErrorV3::NONE;
    YCbCrLayout ycbcr = {};

    auto ret = gralloc3_->lockYCbCr(
      raw_buffer, buffer_usage, buffer_region, fence,
      [&](const auto& lock_error,
          const auto& lock_ycbcr) {
         error = lock_error;
         if (lock_error == ErrorV3::NONE) {
          ycbcr = lock_ycbcr;
        }
       });

    if (!ret.isOk()) {
      error = ErrorV3::NO_RESOURCES;
    }

    if (error != ErrorV3::NONE) {
      ALOGE("%s failed to lock buffer", __FUNCTION__);
      return std::nullopt;
    }

    android_ycbcr buffer_ycbcr;
    buffer_ycbcr.y = ycbcr.y;
    buffer_ycbcr.cb = ycbcr.cb;
    buffer_ycbcr.cr = ycbcr.cr;
    buffer_ycbcr.ystride = ycbcr.yStride;
    buffer_ycbcr.cstride = ycbcr.cStride;
    buffer_ycbcr.chroma_step = ycbcr.chromaStep;

    return buffer_ycbcr;
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
  if (gralloc3_ != nullptr) {
    auto native_handle = const_cast<native_handle_t*>(buffer);

    ErrorV3 error = ErrorV3::NONE;
    auto ret = gralloc3_->unlock(native_handle,
                               [&](const auto& unlock_error, const auto&) {
                                 error = unlock_error;
                               });
    if (!ret.isOk()) {
      error = ErrorV3::NO_RESOURCES;
    }
    if (error != ErrorV3::NONE) {
      ALOGE("%s failed to unlock buffer", __FUNCTION__);
    }
    return;
  }
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
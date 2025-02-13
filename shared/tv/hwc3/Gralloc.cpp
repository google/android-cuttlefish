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

#include "Gralloc.h"

#include <aidl/android/hardware/graphics/common/BufferUsage.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponent.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponentType.h>
#include <drm_fourcc.h>
#include <gralloctypes/Gralloc4.h>
#include <log/log.h>
#include <ui/GraphicBufferMapper.h>

#include <algorithm>

#include "Drm.h"

using aidl::android::hardware::graphics::common::BufferUsage;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::PlaneLayoutComponent;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
using android::GraphicBufferMapper;
using android::OK;
using android::Rect;
using android::status_t;

namespace aidl::android::hardware::graphics::composer3::impl {

std::optional<uint32_t> Gralloc::GetWidth(buffer_handle_t buffer) {
  uint64_t width = 0;
  status_t status = GraphicBufferMapper::get().getWidth(buffer, &width);
  if (status != OK) {
    return std::nullopt;
  }

  if (width > std::numeric_limits<uint32_t>::max()) {
    ALOGE("%s Width too large to cast to uint32_t: %ld", __FUNCTION__, width);
    return std::nullopt;
  }
  return static_cast<uint32_t>(width);
}

std::optional<uint32_t> Gralloc::GetHeight(buffer_handle_t buffer) {
  uint64_t height = 0;
  status_t status = GraphicBufferMapper::get().getHeight(buffer, &height);
  if (status != OK) {
    return std::nullopt;
  }

  if (height > std::numeric_limits<uint32_t>::max()) {
    ALOGE("%s Height too large to cast to uint32_t: %ld", __FUNCTION__, height);
    return std::nullopt;
  }
  return static_cast<uint32_t>(height);
}

std::optional<uint32_t> Gralloc::GetDrmFormat(buffer_handle_t buffer) {
  uint32_t format = 0;
  status_t status =
      GraphicBufferMapper::get().getPixelFormatFourCC(buffer, &format);
  if (status != OK) {
    return std::nullopt;
  }

  return format;
}

std::optional<std::vector<PlaneLayout>> Gralloc::GetPlaneLayouts(
    buffer_handle_t buffer) {
  std::vector<PlaneLayout> layouts;
  status_t status =
      GraphicBufferMapper::get().getPlaneLayouts(buffer, &layouts);
  if (status != OK) {
    return std::nullopt;
  }

  return layouts;
}

std::optional<uint32_t> Gralloc::GetMonoPlanarStrideBytes(
    buffer_handle_t buffer) {
  auto plane_layouts_opt = GetPlaneLayouts(buffer);
  if (!plane_layouts_opt) {
    return std::nullopt;
  }

  std::vector<PlaneLayout>& plane_layouts = *plane_layouts_opt;
  if (plane_layouts.size() != 1) {
    return std::nullopt;
  }

  if (plane_layouts[0].strideInBytes > std::numeric_limits<uint32_t>::max()) {
    ALOGE("%s strideInBytes too large to cast to uint32_t: %ld", __FUNCTION__,
          plane_layouts[0].strideInBytes);
    return std::nullopt;
  }
  return static_cast<uint32_t>(plane_layouts[0].strideInBytes);
}

std::optional<GrallocBuffer> Gralloc::Import(buffer_handle_t buffer) {
  buffer_handle_t imported_buffer;

  status_t status = GraphicBufferMapper::get().importBufferNoValidate(
      buffer, &imported_buffer);

  if (status != OK) {
    ALOGE("%s failed to import buffer: %d", __FUNCTION__, status);
    return std::nullopt;
  }
  return GrallocBuffer(this, imported_buffer);
}

void Gralloc::Release(buffer_handle_t buffer) {
  status_t status = GraphicBufferMapper::get().freeBuffer(buffer);

  if (status != OK) {
    ALOGE("%s failed to release buffer: %d", __FUNCTION__, status);
  }
}

std::optional<void*> Gralloc::Lock(buffer_handle_t buffer) {
  const auto buffer_usage = static_cast<uint64_t>(BufferUsage::CPU_READ_OFTEN) |
                            static_cast<uint64_t>(BufferUsage::CPU_WRITE_OFTEN);

  auto width_opt = GetWidth(buffer);
  if (!width_opt) {
    return std::nullopt;
  }

  auto height_opt = GetHeight(buffer);
  if (!height_opt) {
    return std::nullopt;
  }

  Rect buffer_region;
  buffer_region.left = 0;
  buffer_region.top = 0;
  // width = right - left
  buffer_region.right = static_cast<int32_t>(*width_opt);
  // height = bottom - top
  buffer_region.bottom = static_cast<int32_t>(*height_opt);

  void* data = nullptr;

  status_t status = GraphicBufferMapper::get().lock(buffer, buffer_usage,
                                                    buffer_region, &data);

  if (status != OK) {
    ALOGE("%s failed to lock buffer: %d", __FUNCTION__, status);
    return std::nullopt;
  }

  return data;
}

std::optional<android_ycbcr> Gralloc::LockYCbCr(buffer_handle_t buffer) {
  auto format_opt = GetDrmFormat(buffer);
  if (!format_opt) {
    ALOGE("%s failed to check format of buffer", __FUNCTION__);
    return std::nullopt;
  }

  if (*format_opt != DRM_FORMAT_NV12 && *format_opt != DRM_FORMAT_NV21 &&
      *format_opt != DRM_FORMAT_YVU420) {
    ALOGE("%s called on non-ycbcr buffer", __FUNCTION__);
    return std::nullopt;
  }

  auto lock_opt = Lock(buffer);
  if (!lock_opt) {
    ALOGE("%s failed to lock buffer", __FUNCTION__);
    return std::nullopt;
  }

  auto plane_layouts_opt = GetPlaneLayouts(buffer);
  if (!plane_layouts_opt) {
    ALOGE("%s failed to get plane layouts", __FUNCTION__);
    return std::nullopt;
  }

  android_ycbcr buffer_ycbcr;
  buffer_ycbcr.y = nullptr;
  buffer_ycbcr.cb = nullptr;
  buffer_ycbcr.cr = nullptr;
  buffer_ycbcr.ystride = 0;
  buffer_ycbcr.cstride = 0;
  buffer_ycbcr.chroma_step = 0;

  for (const auto& plane_layout : *plane_layouts_opt) {
    for (const auto& plane_layout_component : plane_layout.components) {
      const auto& type = plane_layout_component.type;

      if (!::android::gralloc4::isStandardPlaneLayoutComponentType(type)) {
        continue;
      }

      auto* component_data = reinterpret_cast<uint8_t*>(*lock_opt) +
                             plane_layout.offsetInBytes +
                             plane_layout_component.offsetInBits / 8;

      switch (static_cast<PlaneLayoutComponentType>(type.value)) {
        case PlaneLayoutComponentType::Y:
          buffer_ycbcr.y = component_data;
          buffer_ycbcr.ystride =
              static_cast<size_t>(plane_layout.strideInBytes);
          break;
        case PlaneLayoutComponentType::CB:
          buffer_ycbcr.cb = component_data;
          buffer_ycbcr.cstride =
              static_cast<size_t>(plane_layout.strideInBytes);
          buffer_ycbcr.chroma_step =
              static_cast<size_t>(plane_layout.sampleIncrementInBits / 8);
          break;
        case PlaneLayoutComponentType::CR:
          buffer_ycbcr.cr = component_data;
          buffer_ycbcr.cstride =
              static_cast<size_t>(plane_layout.strideInBytes);
          buffer_ycbcr.chroma_step =
              static_cast<size_t>(plane_layout.sampleIncrementInBits / 8);
          break;
        default:
          break;
      }
    }
  }

  return buffer_ycbcr;
}

void Gralloc::Unlock(buffer_handle_t buffer) {
  status_t status = GraphicBufferMapper::get().unlock(buffer);

  if (status != OK) {
    ALOGE("%s failed to unlock buffer %d", __FUNCTION__, status);
  }
}

GrallocBuffer::GrallocBuffer(Gralloc* gralloc, buffer_handle_t buffer)
    : gralloc_(gralloc), buffer_(buffer) {}

GrallocBuffer::~GrallocBuffer() { Release(); }

GrallocBuffer::GrallocBuffer(GrallocBuffer&& rhs) { *this = std::move(rhs); }

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

std::optional<GrallocBufferView> GrallocBuffer::Lock() {
  if (gralloc_ && buffer_) {
    auto format_opt = GetDrmFormat();
    if (!format_opt) {
      ALOGE("%s failed to check format of buffer", __FUNCTION__);
      return std::nullopt;
    }
    if (*format_opt != DRM_FORMAT_NV12 && *format_opt != DRM_FORMAT_NV21 &&
        *format_opt != DRM_FORMAT_YVU420) {
      auto locked_opt = gralloc_->Lock(buffer_);
      if (!locked_opt) {
        return std::nullopt;
      }
      return GrallocBufferView(this, *locked_opt);
    } else {
      auto locked_ycbcr_opt = gralloc_->LockYCbCr(buffer_);
      if (!locked_ycbcr_opt) {
        ALOGE("%s failed to lock ycbcr buffer", __FUNCTION__);
        return std::nullopt;
      }
      return GrallocBufferView(this, *locked_ycbcr_opt);
    }
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

std::optional<std::vector<PlaneLayout>> GrallocBuffer::GetPlaneLayouts() {
  if (gralloc_ && buffer_) {
    return gralloc_->GetPlaneLayouts(buffer_);
  }
  return std::nullopt;
}

std::optional<uint32_t> GrallocBuffer::GetMonoPlanarStrideBytes() {
  if (gralloc_ && buffer_) {
    return gralloc_->GetMonoPlanarStrideBytes(buffer_);
  }
  return std::nullopt;
}

GrallocBufferView::GrallocBufferView(GrallocBuffer* buffer, void* raw)
    : gralloc_buffer_(buffer), locked_(raw) {}

GrallocBufferView::GrallocBufferView(GrallocBuffer* buffer, android_ycbcr raw)
    : gralloc_buffer_(buffer), locked_ycbcr_(raw) {}

GrallocBufferView::~GrallocBufferView() {
  if (gralloc_buffer_) {
    gralloc_buffer_->Unlock();
  }
}

GrallocBufferView::GrallocBufferView(GrallocBufferView&& rhs) {
  *this = std::move(rhs);
}

GrallocBufferView& GrallocBufferView::operator=(GrallocBufferView&& rhs) {
  std::swap(gralloc_buffer_, rhs.gralloc_buffer_);
  std::swap(locked_, rhs.locked_);
  std::swap(locked_ycbcr_, rhs.locked_ycbcr_);
  return *this;
}

const std::optional<void*> GrallocBufferView::Get() const { return locked_; }

const std::optional<android_ycbcr>& GrallocBufferView::GetYCbCr() const {
  return locked_ycbcr_;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl

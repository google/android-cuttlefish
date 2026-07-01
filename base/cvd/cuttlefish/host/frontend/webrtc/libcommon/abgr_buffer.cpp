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

#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"

#include <api/video/i420_buffer.h>
#include <libyuv.h>
#include <drm/drm_fourcc.h>

#include "android-base/logging.h"

namespace cuttlefish {

AbgrBuffer::AbgrBuffer(
    std::shared_ptr<cuttlefish::PackedVideoFrameBuffer> buffer)
    : buffer_(std::move(buffer)) {}

int AbgrBuffer::width() const { return buffer_->width(); }
int AbgrBuffer::height() const { return buffer_->height(); }

const uint8_t* AbgrBuffer::Data() const { return buffer_->Data(); }
int AbgrBuffer::Stride() const { return buffer_->Stride(); }

rtc::scoped_refptr<webrtc::I420BufferInterface> AbgrBuffer::ToI420() {
  rtc::scoped_refptr<webrtc::I420Buffer> i420_buffer =
      webrtc::I420Buffer::Create(width(), height());
  
  uint32_t format = buffer_->PixelFormat();

  int res = -1;
  if (format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888) {
    res = libyuv::ARGBToI420(
        buffer_->Data(), buffer_->Stride(),
        i420_buffer->MutableDataY(), i420_buffer->StrideY(),
        i420_buffer->MutableDataU(), i420_buffer->StrideU(),
        i420_buffer->MutableDataV(), i420_buffer->StrideV(),
        width(), height());
  } else if (format == DRM_FORMAT_ABGR8888 ||
             format == DRM_FORMAT_XBGR8888) {
    res = libyuv::ABGRToI420(
        buffer_->Data(), buffer_->Stride(),
        i420_buffer->MutableDataY(), i420_buffer->StrideY(),
        i420_buffer->MutableDataU(), i420_buffer->StrideU(),
        i420_buffer->MutableDataV(), i420_buffer->StrideV(),
        width(), height());
  } else {
    LOG(ERROR) << "AbgrBuffer::ToI420: unsupported pixel format: 0x"
               << std::hex << format;
    return nullptr;
  }

  if (res != 0) {
    LOG(ERROR) << "AbgrBuffer::ToI420: conversion failed: " << res;
    return nullptr;
  }

  return i420_buffer;
}

}  // namespace cuttlefish

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
#include "vsock_frame_provider.h"
#include <hardware/camera3.h>
#include <libyuv.h>
#include <cstring>
#define LOG_TAG "VsockFrameProvider"
#include <log/log.h>

namespace cuttlefish {

namespace {
bool writeJsonEventMessage(
    std::shared_ptr<cuttlefish::VsockConnection> connection,
    const std::string& message) {
  Json::Value json_message;
  json_message["event"] = message;
  return connection && connection->WriteMessage(json_message);
}
}  // namespace

VsockFrameProvider::~VsockFrameProvider() { stop(); }

void VsockFrameProvider::start(
    std::shared_ptr<cuttlefish::VsockConnection> connection, uint32_t width,
    uint32_t height) {
  stop();
  running_ = true;
  connection_ = connection;
  writeJsonEventMessage(connection, "VIRTUAL_DEVICE_START_CAMERA_SESSION");
  reader_thread_ =
      std::thread([this, width, height] { VsockReadLoop(width, height); });
}

void VsockFrameProvider::stop() {
  running_ = false;
  jpeg_pending_ = false;
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  writeJsonEventMessage(connection_, "VIRTUAL_DEVICE_STOP_CAMERA_SESSION");
  connection_ = nullptr;
}

bool VsockFrameProvider::waitYUVFrame(unsigned int max_wait_ms) {
  auto timeout = std::chrono::milliseconds(max_wait_ms);
  nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
  std::unique_lock<std::mutex> lock(frame_mutex_);
  return yuv_frame_updated_.wait_for(
      lock, timeout, [this, now] { return timestamp_.load() > now; });
}

void VsockFrameProvider::requestJpeg() {
  jpeg_pending_ = true;
  writeJsonEventMessage(connection_, "VIRTUAL_DEVICE_CAPTURE_IMAGE");
}

void VsockFrameProvider::cancelJpegRequest() { jpeg_pending_ = false; }

bool VsockFrameProvider::copyYUVFrame(uint32_t w, uint32_t h, YCbCrLayout dst) {
  size_t y_size = w * h;
  size_t cbcr_size = (w / 2) * (h / 2);
  size_t total_size = y_size + 2 * cbcr_size;
  std::lock_guard<std::mutex> lock(frame_mutex_);
  if (frame_.size() < total_size) {
    ALOGE("%s: %zu is too little for %ux%u frame", __FUNCTION__, frame_.size(),
          w, h);
    return false;
  }
  if (dst.y == nullptr) {
    ALOGE("%s: Destination is nullptr!", __FUNCTION__);
    return false;
  }
  YCbCrLayout src{.y = static_cast<void*>(frame_.data()),
                  .cb = static_cast<void*>(frame_.data() + y_size),
                  .cr = static_cast<void*>(frame_.data() + y_size + cbcr_size),
                  .yStride = w,
                  .cStride = w / 2,
                  .chromaStep = 1};
  uint8_t* src_y = static_cast<uint8_t*>(src.y);
  uint8_t* dst_y = static_cast<uint8_t*>(dst.y);
  uint8_t* src_cb = static_cast<uint8_t*>(src.cb);
  uint8_t* dst_cb = static_cast<uint8_t*>(dst.cb);
  uint8_t* src_cr = static_cast<uint8_t*>(src.cr);
  uint8_t* dst_cr = static_cast<uint8_t*>(dst.cr);
  libyuv::CopyPlane(src_y, src.yStride, dst_y, dst.yStride, w, h);
  if (dst.chromaStep == 1) {
    // Planar
    libyuv::CopyPlane(src_cb, src.cStride, dst_cb, dst.cStride, w / 2, h / 2);
    libyuv::CopyPlane(src_cr, src.cStride, dst_cr, dst.cStride, w / 2, h / 2);
  } else if (dst.chromaStep == 2 && dst_cr - dst_cb == 1) {
    // Interleaved cb/cr planes starting with cb
    libyuv::MergeUVPlane(src_cb, src.cStride, src_cr, src.cStride, dst_cb,
                         dst.cStride, w / 2, h / 2);
  } else if (dst.chromaStep == 2 && dst_cb - dst_cr == 1) {
    // Interleaved cb/cr planes starting with cr
    libyuv::MergeUVPlane(src_cr, src.cStride, src_cb, src.cStride, dst_cr,
                         dst.cStride, w / 2, h / 2);
  } else {
    ALOGE("%s: Unsupported interleaved U/V layout", __FUNCTION__);
    return false;
  }
  return true;
}

bool VsockFrameProvider::copyJpegData(uint32_t size, void* dst) {
  std::lock_guard<std::mutex> lock(jpeg_mutex_);
  auto jpeg_header_offset = size - sizeof(struct camera3_jpeg_blob);
  if (cached_jpeg_.empty()) {
    ALOGE("%s: No source data", __FUNCTION__);
    return false;
  } else if (dst == nullptr) {
    ALOGE("%s: Destination is nullptr", __FUNCTION__);
    return false;
  } else if (jpeg_header_offset <= cached_jpeg_.size()) {
    ALOGE("%s: %ubyte target buffer too small", __FUNCTION__, size);
    return false;
  }
  std::memcpy(dst, cached_jpeg_.data(), cached_jpeg_.size());
  struct camera3_jpeg_blob* blob = reinterpret_cast<struct camera3_jpeg_blob*>(
      static_cast<char*>(dst) + jpeg_header_offset);
  blob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
  blob->jpeg_size = cached_jpeg_.size();
  cached_jpeg_.clear();
  return true;
}

bool VsockFrameProvider::isBlob(const std::vector<char>& blob) {
  bool is_png = blob.size() > 4 && (blob[0] & 0xff) == 0x89 &&
                (blob[1] & 0xff) == 0x50 && (blob[2] & 0xff) == 0x4e &&
                (blob[3] & 0xff) == 0x47;
  bool is_jpeg =
      blob.size() > 2 && (blob[0] & 0xff) == 0xff && (blob[1] & 0xff) == 0xd8;
  return is_png || is_jpeg;
}

bool VsockFrameProvider::framesizeMatches(uint32_t width, uint32_t height,
                                          const std::vector<char>& data) {
  return data.size() == 3 * width * height / 2;
}

void VsockFrameProvider::VsockReadLoop(uint32_t width, uint32_t height) {
  jpeg_pending_ = false;
  while (running_.load() && connection_->ReadMessage(next_frame_)) {
    if (framesizeMatches(width, height, next_frame_)) {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      timestamp_ = systemTime();
      frame_.swap(next_frame_);
      yuv_frame_updated_.notify_one();
    } else if (isBlob(next_frame_)) {
      std::lock_guard<std::mutex> lock(jpeg_mutex_);
      bool was_pending = jpeg_pending_.exchange(false);
      if (was_pending) {
        cached_jpeg_.swap(next_frame_);
      }
    } else {
      ALOGE("%s: Unexpected data of %zu bytes", __FUNCTION__,
            next_frame_.size());
    }
  }
  if (!connection_->IsConnected()) {
    ALOGE("%s: Connection closed - exiting", __FUNCTION__);
    running_ = false;
  }
}

}  // namespace cuttlefish

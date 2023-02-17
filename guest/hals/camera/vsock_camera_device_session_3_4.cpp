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
#define LOG_TAG "VsockCameraDeviceSession"
#include "vsock_camera_device_session_3_4.h"
#include <hidl/Status.h>
#include <include/convert.h>
#include <inttypes.h>
#include <libyuv.h>
#include <log/log.h>
#include "vsock_camera_metadata.h"

// Partially copied from ExternalCameraDeviceSession
namespace android::hardware::camera::device::V3_4::implementation {

VsockCameraDeviceSession::VsockCameraDeviceSession(
    VsockCameraMetadata camera_characteristics,
    std::shared_ptr<cuttlefish::VsockFrameProvider> frame_provider,
    const sp<ICameraDeviceCallback>& callback)
    : camera_characteristics_(camera_characteristics),
      frame_provider_(frame_provider),
      callback_(callback) {
  static constexpr size_t kMsgQueueSize = 256 * 1024;
  request_queue_ =
      std::make_unique<MessageQueue<uint8_t, kSynchronizedReadWrite>>(
          kMsgQueueSize, false);
  result_queue_ =
      std::make_shared<MessageQueue<uint8_t, kSynchronizedReadWrite>>(
          kMsgQueueSize, false);
  unsigned int timeout_ms = 1000 / camera_characteristics.getPreferredFps();
  process_requests_ = true;
  request_processor_ =
      std::thread([this, timeout_ms] { processRequestLoop(timeout_ms); });
}

VsockCameraDeviceSession::~VsockCameraDeviceSession() { close(); }

Return<void> VsockCameraDeviceSession::constructDefaultRequestSettings(
    V3_2::RequestTemplate type,
    V3_2::ICameraDeviceSession::constructDefaultRequestSettings_cb _hidl_cb) {
  auto frame_rate = camera_characteristics_.getPreferredFps();
  auto metadata = VsockCameraRequestMetadata(frame_rate, type);
  V3_2::CameraMetadata hidl_metadata;
  Status status = metadata.isValid() ? Status::OK : Status::ILLEGAL_ARGUMENT;
  if (metadata.isValid()) {
    camera_metadata_t* metadata_ptr = metadata.release();
    hidl_metadata.setToExternal((uint8_t*)metadata_ptr,
                                get_camera_metadata_size(metadata_ptr));
  }
  _hidl_cb(status, hidl_metadata);
  return Void();
}

Return<void> VsockCameraDeviceSession::getCaptureRequestMetadataQueue(
    ICameraDeviceSession::getCaptureRequestMetadataQueue_cb _hidl_cb) {
  _hidl_cb(*request_queue_->getDesc());
  return Void();
}

Return<void> VsockCameraDeviceSession::getCaptureResultMetadataQueue(
    ICameraDeviceSession::getCaptureResultMetadataQueue_cb _hidl_cb) {
  _hidl_cb(*result_queue_->getDesc());
  return Void();
}

Return<void> VsockCameraDeviceSession::configureStreams(
    const V3_2::StreamConfiguration& streams,
    ICameraDeviceSession::configureStreams_cb _hidl_cb) {
  // common configureStreams operate with v3_2 config and v3_3
  // streams so we need to "downcast" v3_3 streams to v3_2 streams
  V3_2::HalStreamConfiguration out_v32;
  V3_3::HalStreamConfiguration out_v33;

  Status status = configureStreams(streams, &out_v33);
  size_t size = out_v33.streams.size();
  out_v32.streams.resize(size);
  for (size_t i = 0; i < size; i++) {
    out_v32.streams[i] = out_v33.streams[i].v3_2;
  }
  _hidl_cb(status, out_v32);
  return Void();
}

Return<void> VsockCameraDeviceSession::configureStreams_3_3(
    const V3_2::StreamConfiguration& streams,
    ICameraDeviceSession::configureStreams_3_3_cb _hidl_cb) {
  V3_3::HalStreamConfiguration out_v33;
  Status status = configureStreams(streams, &out_v33);
  _hidl_cb(status, out_v33);
  return Void();
}

Return<void> VsockCameraDeviceSession::configureStreams_3_4(
    const V3_4::StreamConfiguration& requestedConfiguration,
    ICameraDeviceSession::configureStreams_3_4_cb _hidl_cb) {
  // common configureStreams operate with v3_2 config and v3_3
  // streams so we need to "downcast" v3_4 config to v3_2 and
  // "upcast" v3_3 streams to v3_4 streams
  V3_2::StreamConfiguration config_v32;
  V3_3::HalStreamConfiguration out_v33;
  V3_4::HalStreamConfiguration out_v34;

  config_v32.operationMode = requestedConfiguration.operationMode;
  config_v32.streams.resize(requestedConfiguration.streams.size());
  for (size_t i = 0; i < config_v32.streams.size(); i++) {
    config_v32.streams[i] = requestedConfiguration.streams[i].v3_2;
  }
  max_blob_size_ = getBlobSize(requestedConfiguration);
  Status status = configureStreams(config_v32, &out_v33);

  out_v34.streams.resize(out_v33.streams.size());
  for (size_t i = 0; i < out_v34.streams.size(); i++) {
    out_v34.streams[i].v3_3 = out_v33.streams[i];
  }
  _hidl_cb(status, out_v34);
  return Void();
}

Return<void> VsockCameraDeviceSession::processCaptureRequest(
    const hidl_vec<CaptureRequest>& requests,
    const hidl_vec<BufferCache>& cachesToRemove,
    ICameraDeviceSession::processCaptureRequest_cb _hidl_cb) {
  updateBufferCaches(cachesToRemove);

  uint32_t count;
  Status s = Status::OK;
  for (count = 0; count < requests.size(); count++) {
    s = processOneCaptureRequest(requests[count]);
    if (s != Status::OK) {
      break;
    }
  }

  _hidl_cb(s, count);
  return Void();
}

Return<void> VsockCameraDeviceSession::processCaptureRequest_3_4(
    const hidl_vec<V3_4::CaptureRequest>& requests,
    const hidl_vec<V3_2::BufferCache>& cachesToRemove,
    ICameraDeviceSession::processCaptureRequest_3_4_cb _hidl_cb) {
  updateBufferCaches(cachesToRemove);

  uint32_t count;
  Status s = Status::OK;
  for (count = 0; count < requests.size(); count++) {
    s = processOneCaptureRequest(requests[count].v3_2);
    if (s != Status::OK) {
      break;
    }
  }

  _hidl_cb(s, count);
  return Void();
}

Return<Status> VsockCameraDeviceSession::flush() {
  auto timeout = std::chrono::seconds(1);
  std::unique_lock<std::mutex> lock(request_mutex_);
  flushing_requests_ = true;
  auto is_empty = [this] { return pending_requests_.empty(); };
  if (!queue_empty_.wait_for(lock, timeout, is_empty)) {
    ALOGE("Flush timeout - %zu pending requests", pending_requests_.size());
  }
  flushing_requests_ = false;
  return Status::OK;
}

Return<void> VsockCameraDeviceSession::close() {
  process_requests_ = false;
  if (request_processor_.joinable()) {
    request_processor_.join();
  }
  frame_provider_->stop();
  buffer_cache_.clear();
  ALOGI("%s", __FUNCTION__);
  return Void();
}

using ::android::hardware::graphics::common::V1_0::BufferUsage;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
Status VsockCameraDeviceSession::configureStreams(
    const V3_2::StreamConfiguration& config,
    V3_3::HalStreamConfiguration* out) {
  Status status = isStreamConfigurationSupported(config);
  if (status != Status::OK) {
    return status;
  }
  updateStreamInfo(config);
  out->streams.resize(config.streams.size());
  for (size_t i = 0; i < config.streams.size(); i++) {
    out->streams[i].overrideDataSpace = config.streams[i].dataSpace;
    out->streams[i].v3_2.id = config.streams[i].id;
    out->streams[i].v3_2.producerUsage =
        config.streams[i].usage | BufferUsage::CPU_WRITE_OFTEN;
    out->streams[i].v3_2.consumerUsage = 0;
    out->streams[i].v3_2.maxBuffers = 2;
    out->streams[i].v3_2.overrideFormat =
        config.streams[i].format == PixelFormat::IMPLEMENTATION_DEFINED
            ? PixelFormat::YCBCR_420_888
            : config.streams[i].format;
  }
  return Status::OK;
}

using ::android::hardware::camera::device::V3_2::StreamRotation;
using ::android::hardware::camera::device::V3_2::StreamType;
Status VsockCameraDeviceSession::isStreamConfigurationSupported(
    const V3_2::StreamConfiguration& config) {
  camera_metadata_entry device_supported_streams = camera_characteristics_.find(
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
  int32_t stall_stream_count = 0;
  int32_t stream_count = 0;
  for (const auto& stream : config.streams) {
    if (stream.rotation != StreamRotation::ROTATION_0) {
      ALOGE("Unsupported rotation enum value %d", stream.rotation);
      return Status::ILLEGAL_ARGUMENT;
    }
    if (stream.streamType == StreamType::INPUT) {
      ALOGE("Input stream not supported");
      return Status::ILLEGAL_ARGUMENT;
    }
    bool is_supported = false;
    // check pixel format and dimensions against camera metadata
    for (int i = 0; i + 4 <= device_supported_streams.count; i += 4) {
      auto format =
          static_cast<PixelFormat>(device_supported_streams.data.i32[i]);
      int32_t width = device_supported_streams.data.i32[i + 1];
      int32_t height = device_supported_streams.data.i32[i + 2];
      if (stream.format == format && stream.width == width &&
          stream.height == height) {
        is_supported = true;
        break;
      }
    }
    if (!is_supported) {
      ALOGE("Unsupported format %d (%dx%d)", stream.format, stream.width,
            stream.height);
      return Status::ILLEGAL_ARGUMENT;
    }
    if (stream.format == PixelFormat::BLOB) {
      stall_stream_count++;
    } else {
      stream_count++;
    }
  }
  camera_metadata_entry device_stream_counts =
      camera_characteristics_.find(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS);
  static constexpr auto stream_index = 1;
  auto expected_stream_count = device_stream_counts.count > stream_index
                                   ? device_stream_counts.data.i32[stream_index]
                                   : 0;
  if (stream_count > expected_stream_count) {
    ALOGE("Too many processed streams (expect <= %d, got %d)",
          expected_stream_count, stream_count);
    return Status::ILLEGAL_ARGUMENT;
  }
  static constexpr auto stall_stream_index = 2;
  expected_stream_count =
      device_stream_counts.count > stall_stream_index
          ? device_stream_counts.data.i32[stall_stream_index]
          : 0;
  if (stall_stream_count > expected_stream_count) {
    ALOGE("Too many stall streams (expect <= %d, got %d)",
          expected_stream_count, stall_stream_count);
    return Status::ILLEGAL_ARGUMENT;
  }
  return Status::OK;
}

unsigned int VsockCameraDeviceSession::getBlobSize(
    const V3_4::StreamConfiguration& requestedConfiguration) {
  camera_metadata_entry jpeg_entry =
      camera_characteristics_.find(ANDROID_JPEG_MAX_SIZE);
  unsigned int blob_size = jpeg_entry.count > 0 ? jpeg_entry.data.i32[0] : 0;
  for (auto& stream : requestedConfiguration.streams) {
    if (stream.v3_2.format == PixelFormat::BLOB) {
      if (stream.bufferSize < blob_size) {
        blob_size = stream.bufferSize;
      }
    }
  }
  return blob_size;
}

void VsockCameraDeviceSession::updateBufferCaches(
    const hidl_vec<BufferCache>& to_remove) {
  for (auto& cache : to_remove) {
    buffer_cache_.remove(cache.bufferId);
  }
}

void VsockCameraDeviceSession::updateStreamInfo(
    const V3_2::StreamConfiguration& config) {
  std::set<int32_t> stream_ids;
  for (const auto& stream : config.streams) {
    stream_cache_[stream.id] = stream;
    stream_ids.emplace(stream.id);
  }
  buffer_cache_.removeStreamsExcept(stream_ids);
}

Status VsockCameraDeviceSession::processOneCaptureRequest(
    const CaptureRequest& request) {
  const camera_metadata_t* request_settings = nullptr;
  V3_2::CameraMetadata hidl_settings;
  if (request.fmqSettingsSize > 0) {
    if (!getRequestSettingsFmq(request.fmqSettingsSize, hidl_settings)) {
      ALOGE("%s: Could not read capture request settings from fmq!",
            __FUNCTION__);
      return Status::ILLEGAL_ARGUMENT;
    } else if (!V3_2::implementation::convertFromHidl(hidl_settings,
                                                      &request_settings)) {
      ALOGE("%s: fmq request settings metadata is corrupt!", __FUNCTION__);
      return Status::ILLEGAL_ARGUMENT;
    }
  } else if (!V3_2::implementation::convertFromHidl(request.settings,
                                                    &request_settings)) {
    ALOGE("%s: request settings metadata is corrupt!", __FUNCTION__);
    return Status::ILLEGAL_ARGUMENT;
  }
  if (request_settings != nullptr) {
    // Update request settings. This must happen on first request
    std::lock_guard<std::mutex> lock(settings_mutex_);
    latest_request_settings_ = request_settings;
  } else if (latest_request_settings_.isEmpty()) {
    ALOGE("%s: Undefined capture request settings!", __FUNCTION__);
    return Status::ILLEGAL_ARGUMENT;
  }

  std::vector<uint64_t> buffer_ids;
  buffer_ids.reserve(request.outputBuffers.size());
  for (size_t i = 0; i < request.outputBuffers.size(); i++) {
    buffer_cache_.update(request.outputBuffers[i]);
    buffer_ids.emplace_back(request.outputBuffers[i].bufferId);
  }
  std::lock_guard<std::mutex> lock(settings_mutex_);
  ReadVsockRequest request_to_process = {
      .buffer_ids = buffer_ids,
      .frame_number = request.frameNumber,
      .timestamp = 0,
      .settings = latest_request_settings_,
      .buffer_count = static_cast<uint32_t>(buffer_ids.size())};
  putRequestToQueue(request_to_process);
  return Status::OK;
}

bool VsockCameraDeviceSession::getRequestSettingsFmq(
    uint64_t size, V3_2::CameraMetadata& hidl_settings) {
  hidl_settings.resize(size);
  return request_queue_->read(hidl_settings.data(), size);
}

bool VsockCameraDeviceSession::getRequestFromQueue(ReadVsockRequest& req,
                                                   unsigned int timeout_ms) {
  auto timeout = std::chrono::milliseconds(timeout_ms);
  std::unique_lock<std::mutex> lock(request_mutex_);
  auto not_empty = [this] { return !pending_requests_.empty(); };
  if (request_available_.wait_for(lock, timeout, not_empty)) {
    req = pending_requests_.top();
    pending_requests_.pop();
    return true;
  }
  queue_empty_.notify_one();
  return false;
}

void VsockCameraDeviceSession::putRequestToQueue(
    const ReadVsockRequest& request) {
  std::lock_guard<std::mutex> lock(request_mutex_);
  pending_requests_.push(request);
  request_available_.notify_one();
}

void VsockCameraDeviceSession::fillCaptureResult(
    common::V1_0::helper::CameraMetadata& md, nsecs_t timestamp) {
  const uint8_t af_state = ANDROID_CONTROL_AF_STATE_INACTIVE;
  md.update(ANDROID_CONTROL_AF_STATE, &af_state, 1);

  const uint8_t aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
  md.update(ANDROID_CONTROL_AE_STATE, &aeState, 1);

  const uint8_t ae_lock = ANDROID_CONTROL_AE_LOCK_OFF;
  md.update(ANDROID_CONTROL_AE_LOCK, &ae_lock, 1);

  const uint8_t awbState = ANDROID_CONTROL_AWB_STATE_CONVERGED;
  md.update(ANDROID_CONTROL_AWB_STATE, &awbState, 1);

  const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
  md.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

  const uint8_t flashState = ANDROID_FLASH_STATE_UNAVAILABLE;
  md.update(ANDROID_FLASH_STATE, &flashState, 1);

  const uint8_t requestPipelineMaxDepth = 4;
  md.update(ANDROID_REQUEST_PIPELINE_DEPTH, &requestPipelineMaxDepth, 1);

  camera_metadata_entry active_array_size =
      camera_characteristics_.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  md.update(ANDROID_SCALER_CROP_REGION, active_array_size.data.i32, 4);

  md.update(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);

  const uint8_t lensShadingMapMode =
      ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
  md.update(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lensShadingMapMode, 1);

  const uint8_t sceneFlicker = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
  md.update(ANDROID_STATISTICS_SCENE_FLICKER, &sceneFlicker, 1);
}

using ::android::hardware::camera::device::V3_2::MsgType;
using ::android::hardware::camera::device::V3_2::NotifyMsg;
void VsockCameraDeviceSession::notifyShutter(uint32_t frame_number,
                                             nsecs_t timestamp) {
  NotifyMsg msg;
  msg.type = MsgType::SHUTTER;
  msg.msg.shutter.frameNumber = frame_number;
  msg.msg.shutter.timestamp = timestamp;
  callback_->notify({msg});
}

void VsockCameraDeviceSession::notifyError(uint32_t frame_number,
                                           int32_t stream_id, ErrorCode code) {
  NotifyMsg msg;
  msg.type = MsgType::ERROR;
  msg.msg.error.frameNumber = frame_number;
  msg.msg.error.errorStreamId = stream_id;
  msg.msg.error.errorCode = code;
  callback_->notify({msg});
}

void VsockCameraDeviceSession::tryWriteFmqResult(V3_2::CaptureResult& result) {
  result.fmqResultSize = 0;
  if (result_queue_->availableToWrite() == 0 || result.result.size() == 0) {
    return;
  }
  if (result_queue_->write(result.result.data(), result.result.size())) {
    result.fmqResultSize = result.result.size();
    result.result.resize(0);
  }
}

using ::android::hardware::camera::device::V3_2::BufferStatus;
using ::android::hardware::graphics::common::V1_0::PixelFormat;
void VsockCameraDeviceSession::processRequestLoop(
    unsigned int wait_timeout_ms) {
  while (process_requests_.load()) {
    ReadVsockRequest request;
    if (!getRequestFromQueue(request, wait_timeout_ms)) {
      continue;
    }
    if (!frame_provider_->isRunning()) {
      notifyError(request.frame_number, -1, ErrorCode::ERROR_DEVICE);
      break;
    }
    frame_provider_->waitYUVFrame(wait_timeout_ms);
    nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
    if (request.timestamp == 0) {
      request.timestamp = now;
      notifyShutter(request.frame_number, request.timestamp);
    }
    std::vector<ReleaseFence> release_fences;
    std::vector<StreamBuffer> result_buffers;
    std::vector<uint64_t> pending_buffers;
    bool request_ok = true;
    for (auto buffer_id : request.buffer_ids) {
      auto buffer = buffer_cache_.get(buffer_id);
      auto stream_id = buffer ? buffer->streamId() : -1;
      if (!buffer || stream_cache_.count(stream_id) == 0) {
        ALOGE("%s: Invalid buffer", __FUNCTION__);
        notifyError(request.frame_number, -1, ErrorCode::ERROR_REQUEST);
        request_ok = false;
        break;
      }
      bool has_result = false;
      auto stream = stream_cache_[stream_id];
      if (flushing_requests_.load()) {
        has_result = false;
        release_fences.emplace_back(buffer->acquireFence());
      } else if (stream.format == PixelFormat::YCBCR_420_888 ||
                 stream.format == PixelFormat::IMPLEMENTATION_DEFINED) {
        auto dst_yuv =
            buffer->acquireAsYUV(stream.width, stream.height, wait_timeout_ms);
        has_result =
            frame_provider_->copyYUVFrame(stream.width, stream.height, dst_yuv);
        release_fences.emplace_back(buffer->release());
      } else if (stream.format == PixelFormat::BLOB) {
        auto time_elapsed = now - request.timestamp;
        if (time_elapsed == 0) {
          frame_provider_->requestJpeg();
          pending_buffers.push_back(buffer_id);
          continue;
        } else if (frame_provider_->jpegPending()) {
          static constexpr auto kMaxWaitNs = 2000000000L;
          if (time_elapsed < kMaxWaitNs) {
            pending_buffers.push_back(buffer_id);
            continue;
          }
          ALOGE("%s: Blob request timed out after %" PRId64 "ms", __FUNCTION__,
                ns2ms(time_elapsed));
          frame_provider_->cancelJpegRequest();
          has_result = false;
          release_fences.emplace_back(buffer->acquireFence());
          notifyError(request.frame_number, buffer->streamId(),
                      ErrorCode::ERROR_BUFFER);
        } else {
          ALOGI("%s: Blob ready - capture duration=%" PRId64 "ms", __FUNCTION__,
                ns2ms(time_elapsed));
          auto dst_blob =
              buffer->acquireAsBlob(max_blob_size_, wait_timeout_ms);
          has_result = frame_provider_->copyJpegData(max_blob_size_, dst_blob);
          release_fences.emplace_back(buffer->release());
        }
      } else {
        ALOGE("%s: Format %d not supported", __FUNCTION__, stream.format);
        has_result = false;
        release_fences.emplace_back(buffer->acquireFence());
        notifyError(request.frame_number, buffer->streamId(),
                    ErrorCode::ERROR_BUFFER);
      }
      result_buffers.push_back(
          {.streamId = buffer->streamId(),
           .bufferId = buffer->bufferId(),
           .buffer = nullptr,
           .status = has_result ? BufferStatus::OK : BufferStatus::ERROR,
           .releaseFence = release_fences.back().handle()});
    }
    if (!request_ok) {
      continue;
    }

    V3_2::CaptureResult result;
    bool results_filled = request.settings.exists(ANDROID_SENSOR_TIMESTAMP);
    if (!results_filled) {
      fillCaptureResult(request.settings, request.timestamp);
      const camera_metadata_t* metadata = request.settings.getAndLock();
      V3_2::implementation::convertToHidl(metadata, &result.result);
      request.settings.unlock(metadata);
      tryWriteFmqResult(result);
    }
    if (!result_buffers.empty() || !results_filled) {
      result.frameNumber = request.frame_number;
      result.partialResult = !results_filled ? 1 : 0;
      result.inputBuffer.streamId = -1;
      result.outputBuffers = result_buffers;
      std::vector<V3_2::CaptureResult> results{result};
      auto status = callback_->processCaptureResult(results);
      release_fences.clear();
      if (!status.isOk()) {
        ALOGE("%s: processCaptureResult error: %s", __FUNCTION__,
              status.description().c_str());
      }
    }
    if (!pending_buffers.empty()) {
      // some buffers still pending
      request.buffer_ids = pending_buffers;
      putRequestToQueue(request);
    }
  }
}

}  // namespace android::hardware::camera::device::V3_4::implementation

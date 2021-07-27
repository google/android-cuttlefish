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
#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <android/hardware/camera/device/3.4/ICameraDeviceSession.h>
#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <fmq/MessageQueue.h>
#include <queue>
#include <thread>
#include "stream_buffer_cache.h"
#include "vsock_camera_metadata.h"
#include "vsock_frame_provider.h"

namespace android::hardware::camera::device::V3_4::implementation {
using ::android::sp;
using ::android::hardware::hidl_vec;
using ::android::hardware::kSynchronizedReadWrite;
using ::android::hardware::MessageQueue;
using ::android::hardware::Return;
using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::device::V3_2::BufferCache;
using ::android::hardware::camera::device::V3_2::CaptureRequest;
using ::android::hardware::camera::device::V3_2::ErrorCode;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;
using ::android::hardware::camera::device::V3_2::RequestTemplate;
using ::android::hardware::camera::device::V3_2::Stream;
using ::android::hardware::camera::device::V3_4::ICameraDeviceSession;
using ::android::hardware::camera::device::V3_4::StreamConfiguration;
using ::android::hardware::graphics::mapper::V2_0::YCbCrLayout;

class VsockCameraDeviceSession : public ICameraDeviceSession {
 public:
  VsockCameraDeviceSession(
      VsockCameraMetadata camera_characteristics,
      std::shared_ptr<cuttlefish::VsockFrameProvider> frame_provider,
      const sp<ICameraDeviceCallback>& callback);

  ~VsockCameraDeviceSession();

  Return<void> constructDefaultRequestSettings(
      RequestTemplate,
      ICameraDeviceSession::constructDefaultRequestSettings_cb _hidl_cb);

  Return<void> configureStreams(const V3_2::StreamConfiguration&,
                                ICameraDeviceSession::configureStreams_cb);

  Return<void> configureStreams_3_3(
      const V3_2::StreamConfiguration&,
      ICameraDeviceSession::configureStreams_3_3_cb);

  Return<void> configureStreams_3_4(
      const V3_4::StreamConfiguration& requestedConfiguration,
      ICameraDeviceSession::configureStreams_3_4_cb _hidl_cb);

  Return<void> getCaptureRequestMetadataQueue(
      ICameraDeviceSession::getCaptureRequestMetadataQueue_cb);

  Return<void> getCaptureResultMetadataQueue(
      ICameraDeviceSession::getCaptureResultMetadataQueue_cb);

  Return<void> processCaptureRequest(
      const hidl_vec<CaptureRequest>&, const hidl_vec<BufferCache>&,
      ICameraDeviceSession::processCaptureRequest_cb);

  Return<Status> flush();
  Return<void> close();

  Return<void> processCaptureRequest_3_4(
      const hidl_vec<V3_4::CaptureRequest>& requests,
      const hidl_vec<V3_2::BufferCache>& cachesToRemove,
      ICameraDeviceSession::processCaptureRequest_3_4_cb _hidl_cb);

 private:
  struct ReadVsockRequest {
    std::vector<uint64_t> buffer_ids;
    uint32_t frame_number;
    nsecs_t timestamp;
    common::V1_0::helper::CameraMetadata settings;
    uint32_t buffer_count;
  };
  struct VsockRequestComparator {
    bool operator()(const ReadVsockRequest& lhs, const ReadVsockRequest& rhs) {
      return lhs.frame_number > rhs.frame_number;
    }
  };
  void updateBufferCaches(const hidl_vec<BufferCache>& to_remove);
  Status configureStreams(const V3_2::StreamConfiguration& config,
                          V3_3::HalStreamConfiguration* out);
  unsigned int getBlobSize(
      const V3_4::StreamConfiguration& requestedConfiguration);
  Status isStreamConfigurationSupported(
      const V3_2::StreamConfiguration& config);
  void updateStreamInfo(const V3_2::StreamConfiguration& config);
  Status processOneCaptureRequest(const CaptureRequest& request);
  bool getRequestSettingsFmq(uint64_t size,
                             V3_2::CameraMetadata& hidl_settings);
  void processRequestLoop(unsigned int timeout);
  bool getRequestFromQueue(ReadVsockRequest& request, unsigned int timeout_ms);
  void putRequestToQueue(const ReadVsockRequest& request);
  void fillCaptureResult(common::V1_0::helper::CameraMetadata& md,
                         nsecs_t timestamp);
  void notifyShutter(uint32_t frame_number, nsecs_t timestamp);
  void notifyError(uint32_t frame_number, int32_t stream_id, ErrorCode code);
  void tryWriteFmqResult(V3_2::CaptureResult& result);
  VsockCameraMetadata camera_characteristics_;
  std::shared_ptr<cuttlefish::VsockFrameProvider> frame_provider_;
  const sp<ICameraDeviceCallback> callback_;
  std::unique_ptr<MessageQueue<uint8_t, kSynchronizedReadWrite>> request_queue_;
  std::shared_ptr<MessageQueue<uint8_t, kSynchronizedReadWrite>> result_queue_;
  std::mutex settings_mutex_;
  common::V1_0::helper::CameraMetadata latest_request_settings_;

  StreamBufferCache buffer_cache_;
  std::map<int32_t, Stream> stream_cache_;

  std::mutex request_mutex_;
  std::condition_variable request_available_;
  std::condition_variable queue_empty_;
  std::priority_queue<ReadVsockRequest, std::vector<ReadVsockRequest>,
                      VsockRequestComparator>
      pending_requests_;
  std::thread request_processor_;
  std::atomic<bool> process_requests_;
  std::atomic<bool> flushing_requests_;

  unsigned int max_blob_size_;
};

}  // namespace android::hardware::camera::device::V3_4::implementation

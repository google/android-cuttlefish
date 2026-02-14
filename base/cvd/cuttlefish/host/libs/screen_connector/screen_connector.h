/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <stdint.h>

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <fruit/fruit.h>
#include "absl/log/log.h"

#include "cuttlefish/common/libs/confui/confui.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/confui/host_mode_ctrl.h"
#include "cuttlefish/host/libs/confui/host_utils.h"
#include "cuttlefish/host/libs/screen_connector/screen_connector_common.h"
#include "cuttlefish/host/libs/screen_connector/screen_connector_multiplexer.h"
#include "cuttlefish/host/libs/screen_connector/screen_connector_queue.h"
#include "cuttlefish/host/libs/screen_connector/wayland_screen_connector.h"

namespace cuttlefish {

template <typename ProcessedFrameType>
class ScreenConnector : public ScreenConnectorFrameRenderer {
 public:
  static_assert(is_movable<ProcessedFrameType>::value,
                "ProcessedFrameType should be std::move-able.");
  static_assert(
      std::is_base_of<ScreenConnectorFrameInfo, ProcessedFrameType>::value,
      "ProcessedFrameType should inherit ScreenConnectorFrameInfo");

  using FrameMultiplexer = ScreenConnectorInputMultiplexer<ProcessedFrameType>;

  INJECT(ScreenConnector(WaylandScreenConnector& sc_android_src,
                         HostModeCtrl& host_mode_ctrl))
      : sc_android_src_(sc_android_src),
        host_mode_ctrl_{host_mode_ctrl},
        on_next_frame_cnt_{0},
        render_confui_cnt_{0},
        sc_frame_multiplexer_{host_mode_ctrl_} {
    auto config = CuttlefishConfig::Get();
    if (!config) {
      LOG(FATAL) << "CuttlefishConfig is not available.";
    }
    auto instance = config->ForDefaultInstance();
    std::unordered_set<GpuMode> valid_gpu_modes{
        GpuMode::Custom,
        GpuMode::DrmVirgl,
        GpuMode::Gfxstream,
        GpuMode::GfxstreamGuestAngle,
        GpuMode::GfxstreamGuestAngleHostSwiftshader,
        GpuMode::GfxstreamGuestAngleHostLavapipe,
        GpuMode::GuestSwiftshader,
    };
    if (!Contains(valid_gpu_modes, instance.gpu_mode())) {
      LOG(FATAL) << "Invalid gpu mode: " << GpuModeString(instance.gpu_mode());
    }
  }

  /**
   * This is the type of the callback function WebRTC is supposed to provide
   * ScreenConnector with.
   *
   * The callback function is how a raw bytes frame should be processed for
   * WebRTC
   *
   */
  using GenerateProcessedFrameCallback = std::function<void(
      uint32_t /*display_number*/, uint32_t /*frame_width*/,
      uint32_t /*frame_height*/, uint32_t /*frame_fourcc_format*/,
      uint32_t /*frame_stride_bytes*/, uint8_t* /*frame_bytes*/,
      /* ScImpl enqueues this type into the Q */
      ProcessedFrameType& msg)>;

  virtual ~ScreenConnector() = default;

  /**
   * set the callback function to be eventually used by Wayland-Based
   * Connector
   *
   */
  void SetCallback(GenerateProcessedFrameCallback&& frame_callback) {
    std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
    callback_from_streamer_ = std::move(frame_callback);
    streamer_callback_set_cv_.notify_all();

    sc_android_src_.SetFrameCallback(
        [this](uint32_t display_number, uint32_t frame_w, uint32_t frame_h,
               uint32_t frame_fourcc_format, uint32_t frame_stride_bytes,
               uint8_t* frame_bytes) {
          InjectFrame(display_number, frame_w, frame_h, frame_fourcc_format,
                      frame_stride_bytes, frame_bytes);
        });
  }

  void InjectFrame(uint32_t display_number, uint32_t frame_w, uint32_t frame_h,
                   uint32_t frame_fourcc_format, uint32_t frame_stride_bytes,
                   uint8_t* frame_bytes) {
    const bool is_confui_mode = host_mode_ctrl_.IsConfirmatioUiMode();
    if (is_confui_mode) {
      return;
    }

    ProcessedFrameType processed_frame;

    {
      std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
      callback_from_streamer_(display_number, frame_w, frame_h,
                              frame_fourcc_format, frame_stride_bytes,
                              frame_bytes, processed_frame);
    }

    sc_frame_multiplexer_.PushToAndroidQueue(std::move(processed_frame));
  }

  bool IsCallbackSet() const override {
    if (callback_from_streamer_) {
      return true;
    }
    return false;
  }

  void SetDisplayEventCallback(DisplayEventCallback event_callback) {
    sc_android_src_.SetDisplayEventCallback(std::move(event_callback));
  }

  /* returns the processed frame that also includes meta-info such as
   * success/fail and display number from the guest
   *
   * NOTE THAT THIS IS THE ONLY CONSUMER OF THE TWO QUEUES
   */
  ProcessedFrameType OnNextFrame() { return sc_frame_multiplexer_.Pop(); }

  /**
   * ConfUi calls this when it has frames to render
   *
   * This won't be called if not by Confirmation UI. This won't affect rendering
   * Android guest frames if Confirmation UI HAL is not active.
   *
   */
  bool RenderConfirmationUi(uint32_t display_number, uint32_t frame_width,
                            uint32_t frame_height, uint32_t frame_fourcc_format,
                            uint32_t frame_stride_bytes,
                            uint8_t* frame_bytes) override {
    render_confui_cnt_++;
    // wait callback is not set, the streamer is not ready
    // return with LOG(ERROR)
    if (!IsCallbackSet()) {
      ConfUiLog(ERROR) << "callback function to process frames is not yet set";
      return false;
    }
    ProcessedFrameType processed_frame;
    auto this_thread_name = confui::thread::GetName();
    ConfUiLogDebug << this_thread_name
                   << "is sending a #" + std::to_string(render_confui_cnt_)
                   << "Conf UI frame";
    callback_from_streamer_(display_number, frame_width, frame_height,
                            frame_fourcc_format, frame_stride_bytes,
                            frame_bytes, processed_frame);
    // now add processed_frame to the queue
    sc_frame_multiplexer_.PushToConfUiQueue(std::move(processed_frame));
    return true;
  }

 protected:
  ScreenConnector() = delete;

 private:
  WaylandScreenConnector& sc_android_src_;
  HostModeCtrl& host_mode_ctrl_;
  unsigned long long int on_next_frame_cnt_;
  unsigned long long int render_confui_cnt_;
  /**
   * internally has conf ui & android queues.
   *
   * multiplexting the two input queues, so the consumer gets one input
   * at a time from the right queue
   */
  FrameMultiplexer sc_frame_multiplexer_;
  GenerateProcessedFrameCallback callback_from_streamer_;
  std::mutex
      streamer_callback_mutex_;  // mutex to set & read callback_from_streamer_
  std::condition_variable streamer_callback_set_cv_;
};

}  // namespace cuttlefish

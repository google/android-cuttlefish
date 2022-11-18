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

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

#include <android-base/logging.h>

#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/size_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_utils.h"
#include "host/libs/screen_connector/screen_connector_common.h"
#include "host/libs/screen_connector/screen_connector_multiplexer.h"
#include "host/libs/screen_connector/screen_connector_queue.h"
#include "host/libs/screen_connector/wayland_screen_connector.h"

namespace cuttlefish {

template <typename ProcessedFrameType>
class ScreenConnector : public ScreenConnectorInfo,
                        public ScreenConnectorFrameRenderer {
 public:
  static_assert(cuttlefish::is_movable<ProcessedFrameType>::value,
                "ProcessedFrameType should be std::move-able.");
  static_assert(std::is_base_of<ScreenConnectorFrameInfo, ProcessedFrameType>::value,
                "ProcessedFrameType should inherit ScreenConnectorFrameInfo");

  using FrameMultiplexer = ScreenConnectorInputMultiplexer<ProcessedFrameType>;

  /**
   * This is the type of the callback function WebRTC is supposed to provide
   * ScreenConnector with.
   *
   * The callback function is how a raw bytes frame should be processed for
   * WebRTC
   *
   */
  using GenerateProcessedFrameCallback = std::function<void(
      std::uint32_t /*display_number*/, std::uint32_t /*frame_width*/,
      std::uint32_t /*frame_height*/, std::uint32_t /*frame_stride_bytes*/,
      std::uint8_t* /*frame_bytes*/,
      /* ScImpl enqueues this type into the Q */
      ProcessedFrameType& msg)>;

  static std::unique_ptr<ScreenConnector<ProcessedFrameType>> Get(
      const int frames_fd, HostModeCtrl& host_mode_ctrl) {
    auto config = cuttlefish::CuttlefishConfig::Get();
    auto instance = config->ForDefaultInstance();
    ScreenConnector<ProcessedFrameType>* raw_ptr = nullptr;
    if (instance.gpu_mode() == cuttlefish::kGpuModeDrmVirgl ||
        instance.gpu_mode() == cuttlefish::kGpuModeGfxStream ||
        instance.gpu_mode() == cuttlefish::kGpuModeGuestSwiftshader) {
      raw_ptr = new ScreenConnector<ProcessedFrameType>(
          std::make_unique<WaylandScreenConnector>(frames_fd), host_mode_ctrl);
    } else {
      LOG(FATAL) << "Invalid gpu mode: " << instance.gpu_mode();
    }
    return std::unique_ptr<ScreenConnector<ProcessedFrameType>>(raw_ptr);
  }

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

    sc_android_src_->SetFrameCallback(
        [this](std::uint32_t display_number, std::uint32_t frame_w,
               std::uint32_t frame_h, std::uint32_t frame_stride_bytes,
               std::uint8_t* frame_bytes) {
          const bool is_confui_mode = host_mode_ctrl_.IsConfirmatioUiMode();
          if (is_confui_mode) {
            return;
          }

          ProcessedFrameType processed_frame;

          {
            std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
            callback_from_streamer_(display_number, frame_w, frame_h,
                                    frame_stride_bytes, frame_bytes,
                                    processed_frame);
          }

          sc_frame_multiplexer_.PushToAndroidQueue(std::move(processed_frame));
        });
  }

  bool IsCallbackSet() const override {
    if (callback_from_streamer_) {
      return true;
    }
    return false;
  }

  void SetDisplayEventCallback(DisplayEventCallback event_callback) {
    sc_android_src_->SetDisplayEventCallback(std::move(event_callback));
  }

  /* returns the processed frame that also includes meta-info such as success/fail
   * and display number from the guest
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
  bool RenderConfirmationUi(std::uint32_t display_number,
                            std::uint32_t frame_width,
                            std::uint32_t frame_height,
                            std::uint32_t frame_stride_bytes,
                            std::uint8_t* frame_bytes) override {
    render_confui_cnt_++;
    // wait callback is not set, the streamer is not ready
    // return with LOG(ERROR)
    if (!IsCallbackSet()) {
      ConfUiLog(ERROR) << "callback function to process frames is not yet set";
      return false;
    }
    ProcessedFrameType processed_frame;
    auto this_thread_name = cuttlefish::confui::thread::GetName();
    ConfUiLog(DEBUG) << this_thread_name
                     << "is sending a #" + std::to_string(render_confui_cnt_)
                     << "Conf UI frame";
    callback_from_streamer_(display_number, frame_width, frame_height,
                            frame_stride_bytes, frame_bytes, processed_frame);
    // now add processed_frame to the queue
    sc_frame_multiplexer_.PushToConfUiQueue(std::move(processed_frame));
    return true;
  }

 protected:
  ScreenConnector(std::unique_ptr<WaylandScreenConnector>&& impl,
                  HostModeCtrl& host_mode_ctrl)
      : sc_android_src_{std::move(impl)},
        host_mode_ctrl_{host_mode_ctrl},
        on_next_frame_cnt_{0},
        render_confui_cnt_{0},
        sc_frame_multiplexer_{host_mode_ctrl_} {}
  ScreenConnector() = delete;

 private:
  std::unique_ptr<WaylandScreenConnector> sc_android_src_;
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
  std::mutex streamer_callback_mutex_; // mutex to set & read callback_from_streamer_
  std::condition_variable streamer_callback_set_cv_;
};

}  // namespace cuttlefish

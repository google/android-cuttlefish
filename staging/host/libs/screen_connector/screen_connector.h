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

#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

#include <android-base/logging.h>
#include "common/libs/concurrency/semaphore.h"
#include "common/libs/confui/confui.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/size_utils.h"

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_utils.h"
#include "host/libs/screen_connector/screen_connector_common.h"
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

  /**
   * This is the type of the callback function WebRTC/VNC is supposed to provide
   * ScreenConnector with.
   *
   * The callback function should be defined so that the two parameters are
   * given by the callback function caller (e.g. ScreenConnectorSource) and used
   * to fill out the ProcessedFrameType object, msg.
   *
   * The ProcessedFrameType object is internally created by ScreenConnector,
   * filled out by the ScreenConnectorSource, and returned via OnNextFrame()
   * call.
   */
  using GenerateProcessedFrameCallback = std::function<void(
      std::uint32_t /*display_number*/, std::uint8_t* /*frame_pixels*/,
      /* ScImpl enqueues this type into the Q */
      ProcessedFrameType& msg)>;

  static std::unique_ptr<ScreenConnector<ProcessedFrameType>> Get(
      const int frames_fd, HostModeCtrl& host_mode_ctrl) {
    auto config = cuttlefish::CuttlefishConfig::Get();
    ScreenConnector<ProcessedFrameType>* raw_ptr = nullptr;
    if (config->gpu_mode() == cuttlefish::kGpuModeDrmVirgl ||
        config->gpu_mode() == cuttlefish::kGpuModeGfxStream ||
        config->gpu_mode() == cuttlefish::kGpuModeGuestSwiftshader) {
      raw_ptr = new ScreenConnector<ProcessedFrameType>(
          std::make_unique<WaylandScreenConnector>(frames_fd), host_mode_ctrl);
    } else {
      LOG(FATAL) << "Invalid gpu mode: " << config->gpu_mode();
    }
    return std::unique_ptr<ScreenConnector<ProcessedFrameType>>(raw_ptr);
  }

  virtual ~ScreenConnector() = default;

  /**
   * set the callback function to be eventually used by Wayland/Socket-Based Connectors
   *
   * @param[in] To tell how ScreenConnectorSource caches the frame & meta info
   */
  void SetCallback(GenerateProcessedFrameCallback&& frame_callback) {
    std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
    callback_from_streamer_ = std::move(frame_callback);
    streamer_callback_set_cv_.notify_all();
    /*
     * the first WaitForAtLeastOneClientConnection() call from VNC requires the
     * Android-frame-processing thread starts beforehands (b/178504150)
     */
    if (!sc_android_frame_fetching_thread_.joinable()) {
      sc_android_frame_fetching_thread_ = cuttlefish::confui::thread::RunThread(
          "AndroidFetcher", &ScreenConnector::AndroidFrameFetchingLoop, this);
    }
  }

  bool IsCallbackSet() const override {
    if (callback_from_streamer_) {
      return true;
    }
    return false;
  }

  /* returns the processed frame that also includes meta-info such as success/fail
   * and display number from the guest
   *
   * NOTE THAT THIS IS THE ONLY CONSUMER OF THE TWO QUEUES
   */
  ProcessedFrameType OnNextFrame() {
    on_next_frame_cnt_++;
    while (true) {
      ConfUiLog(VERBOSE) << "Streamer waiting Semaphore with host ctrl mode ="
                         << static_cast<std::uint32_t>(
                                host_mode_ctrl_.GetMode())
                         << " and cnd = #" << on_next_frame_cnt_;
      sc_sem_.SemWait();
      ConfUiLog(VERBOSE)
          << "Streamer got Semaphore'ed resources with host ctrl mode ="
          << static_cast<std::uint32_t>(host_mode_ctrl_.GetMode())
          << "and cnd = #" << on_next_frame_cnt_;
      // do something
      if (!sc_android_queue_.Empty()) {
        auto mode = host_mode_ctrl_.GetMode();
        if (mode == HostModeCtrl::ModeType::kAndroidMode) {
          ConfUiLog(VERBOSE)
              << "Streamer gets Android frame with host ctrl mode ="
              << static_cast<std::uint32_t>(mode) << "and cnd = #"
              << on_next_frame_cnt_;
          return sc_android_queue_.PopFront();
        }
        // AndroidFrameFetchingLoop could have added 1 or 2 frames
        // before it becomes Conf UI mode.
        ConfUiLog(VERBOSE)
            << "Streamer ignores Android frame with host ctrl mode ="
            << static_cast<std::uint32_t>(mode) << "and cnd = #"
            << on_next_frame_cnt_;
        sc_android_queue_.PopFront();
        continue;
      }
      ConfUiLog(VERBOSE) << "Streamer gets Conf UI frame with host ctrl mode = "
                         << static_cast<std::uint32_t>(
                                host_mode_ctrl_.GetMode())
                         << " and cnd = #" << on_next_frame_cnt_;
      return sc_confui_queue_.PopFront();
    }
  }

  [[noreturn]] void AndroidFrameFetchingLoop() {
    unsigned long long int loop_cnt = 0;
    cuttlefish::confui::thread::Set("AndroidFrameFetcher",
                                    std::this_thread::get_id());
    while (true) {
      loop_cnt++;
      ProcessedFrameType processed_frame;
      decltype(callback_from_streamer_) cp_of_streamer_callback;
      {
        std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
        cp_of_streamer_callback = callback_from_streamer_;
      }
      GenerateProcessedFrameCallbackImpl callback_for_sc_impl =
          std::bind(cp_of_streamer_callback, std::placeholders::_1,
                    std::placeholders::_2, std::ref(processed_frame));
      ConfUiLog(VERBOSE) << cuttlefish::confui::thread::GetName(
                                std::this_thread::get_id())
                         << " calling Android OnNextFrame. "
                         << " at loop #" << loop_cnt;
      bool flag = sc_android_src_->OnNextFrame(callback_for_sc_impl);
      processed_frame.is_success_ = flag && processed_frame.is_success_;
      const bool is_confui_mode = host_mode_ctrl_.IsConfirmatioUiMode();
      if (!is_confui_mode) {
        ConfUiLog(VERBOSE) << cuttlefish::confui::thread::GetName(
                                  std::this_thread::get_id())
                           << "is sending an Android Frame at loop_cnt #"
                           << loop_cnt;
        sc_android_queue_.PushBack(std::move(processed_frame));
        continue;
      }
      ConfUiLog(VERBOSE) << cuttlefish::confui::thread::GetName(
                                std::this_thread::get_id())
                         << "is skipping an Android Frame at loop_cnt #"
                         << loop_cnt;
    }
  }

  /**
   * ConfUi calls this when it has frames to render
   *
   * This won't be called if not by Confirmation UI. This won't affect rendering
   * Android guest frames if Confirmation UI HAL is not active.
   *
   */
  bool RenderConfirmationUi(const std::uint32_t display,
                            std::uint8_t* raw_frame) override {
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
    callback_from_streamer_(display, raw_frame, processed_frame);
    // now add processed_frame to the queue
    sc_confui_queue_.PushBack(std::move(processed_frame));
    return true;
  }

  // Let the screen connector know when there are clients connected
  void ReportClientsConnected(bool have_clients) {
    // screen connector implementation must implement ReportClientsConnected
    sc_android_src_->ReportClientsConnected(have_clients);
    return ;
  }

 protected:
  template <typename T,
            typename = std::enable_if_t<
                std::is_base_of<ScreenConnectorSource, T>::value, void>>
  ScreenConnector(std::unique_ptr<T>&& impl, HostModeCtrl& host_mode_ctrl)
      : sc_android_src_{std::move(impl)},
        host_mode_ctrl_{host_mode_ctrl},
        on_next_frame_cnt_{0},
        render_confui_cnt_{0},
        sc_android_queue_{sc_sem_},
        sc_confui_queue_{sc_sem_} {}
  ScreenConnector() = delete;

 private:
  // either socket_based or wayland
  std::unique_ptr<ScreenConnectorSource> sc_android_src_;
  HostModeCtrl& host_mode_ctrl_;
  unsigned long long int on_next_frame_cnt_;
  unsigned long long int render_confui_cnt_;
  Semaphore sc_sem_;
  ScreenConnectorQueue<ProcessedFrameType> sc_android_queue_;
  ScreenConnectorQueue<ProcessedFrameType> sc_confui_queue_;
  GenerateProcessedFrameCallback callback_from_streamer_;
  std::thread sc_android_frame_fetching_thread_;
  std::mutex streamer_callback_mutex_; // mutex to set & read callback_from_streamer_
  std::condition_variable streamer_callback_set_cv_;
};

}  // namespace cuttlefish

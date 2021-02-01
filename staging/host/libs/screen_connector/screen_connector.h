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
#include <type_traits>
#include <cassert>
#include <thread>
#include <mutex>

#include <android-base/logging.h>
#include "common/libs/utils/size_utils.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/screen_connector/screen_connector_common.h"
#include "host/libs/screen_connector/screen_connector_queue.h"
#include "host/libs/screen_connector/screen_connector_ctrl.h"
#include "host/libs/screen_connector/socket_based_screen_connector.h"
#include "host/libs/screen_connector/wayland_screen_connector.h"

namespace cuttlefish {

template<typename ProcessedFrameType>
class ScreenConnector : public ScreenConnectorInfo {
 public:
  static_assert(cuttlefish::is_movable<ProcessedFrameType>::value,
                "ProcessedFrameType should be std::move-able.");
  static_assert(std::is_base_of<ScreenConnectorFrameInfo, ProcessedFrameType>::value,
                "ProcessedFrameType should inherit ScreenConnectorFrameInfo");

  /**
   * This is the type of the callback function WebRTC/VNC is supposed to provide
   * ScreenConnector with.
   *
   * The callback function should be defined so that the two parameters are given
   * by the callback function caller (e.g. ScreenConnectorSource) and used to fill
   * out the ProcessedFrameType object, msg.
   *
   * The ProcessedFrameType object is internally created by ScreenConnector, filled
   * out by the ScreenConnectorSource, and returned via OnFrameAfter() call.
   */
  using GenerateProcessedFrameCallback = std::function<void(std::uint32_t /*frame number*/,
                                                            std::uint8_t* /*frame_pixels*/,
                                                            ProcessedFrameType& msg)>;

  static std::unique_ptr<ScreenConnector<ProcessedFrameType>> Get(const int frames_fd) {
    auto config = cuttlefish::CuttlefishConfig::Get();
    ScreenConnector<ProcessedFrameType>* raw_ptr = nullptr;
    if (config->gpu_mode() == cuttlefish::kGpuModeDrmVirgl ||
        config->gpu_mode() == cuttlefish::kGpuModeGfxStream) {
      raw_ptr = new ScreenConnector<ProcessedFrameType>(std::make_unique<WaylandScreenConnector>(frames_fd));
    } else if (config->gpu_mode() == cuttlefish::kGpuModeGuestSwiftshader) {
      raw_ptr = new ScreenConnector<ProcessedFrameType>(std::make_unique<SocketBasedScreenConnector>(frames_fd));
    } else {
      LOG(FATAL) << "Invalid gpu mode: " << config->gpu_mode();
    }
    return std::unique_ptr<ScreenConnector<ProcessedFrameType>>(raw_ptr);
  }

  ~ScreenConnector() = default;

  /**
   * set the callback function to be eventually used by Wayland/Socket-Based Connectors
   *
   * @param[in] To tell how ScreenConnectorSource caches the frame & meta info
   */
  void SetCallback(GenerateProcessedFrameCallback&& frame_callback) {
    std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
    callback_from_streamer_ = std::move(frame_callback);
    /*
     * the first WaitForAtLeastOneClientConnection() call from VNC requires the
     * Android-frame-processing thread starts beforehands (b/178504150)
     */
    if (!is_frame_fetching_thread_started_) {
      is_frame_fetching_thread_started_ = true;
      sc_android_impl_fetcher_ =
          std::move(std::thread(&ScreenConnector::FrameFetchingLoop, this));
    }
  }

  bool IsCallbackSet() const {
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
  ProcessedFrameType OnFrameAfter() {
    // sc_ctrl has a semaphore internally
    // passing beyond SemWait means either queue has an item
    sc_ctrl_.SemWaitItem();
    return sc_android_queue_.PopFront();

    // TODO: add confirmation ui
    /*
     * if (!sc_android_queue_.Empty()) return sc_android_queue_.PopFront();
     * else return conf_ui_queue.PopFront();
     */
  }

  [[noreturn]] void FrameFetchingLoop() {
    std::uint32_t frame_num = 0;
    while (true) {
      sc_ctrl_.WaitAndroidMode( /* pass method to stop sc_android_impl_ */);
      /*
       * TODO: instead of WaitAndroidMode,
       * we could sc_android_impl_->OnFrameAfter but enqueue it only in AndroidMode
       */
      ProcessedFrameType msg;
      decltype(callback_from_streamer_) cp_of_streamer_callback;
      {
        std::lock_guard<std::mutex> lock(streamer_callback_mutex_);
        cp_of_streamer_callback = callback_from_streamer_;
      }
      GenerateProcessedFrameCallbackImpl callback_for_sc_impl =
          std::bind(cp_of_streamer_callback,
                    std::placeholders::_1, std::placeholders::_2,
                    std::ref(msg));
      bool flag = sc_android_impl_->OnFrameAfter(frame_num, callback_for_sc_impl);
      msg.is_success_ = flag && msg.is_success_;
      auto result = ProcessedFrameType{std::move(msg)};
      sc_android_queue_.PushBack(std::move(result));
      ++frame_num;
    }
  }
  // TODO: add ConfUIFetchingLoop

  // Let the screen connector know when there are clients connected
  void ReportClientsConnected(bool have_clients) {
    // screen connector implementation must implement ReportClientsConnected
    sc_android_impl_->ReportClientsConnected(have_clients);
    return ;
  }

 protected:
  template <typename T,
            typename = std::enable_if_t<
                std::is_base_of<ScreenConnectorSource, T>::value, void>>
  ScreenConnector(std::unique_ptr<T>&& impl)
      : sc_android_impl_{std::move(impl)},
        is_frame_fetching_thread_started_(false),
        sc_android_queue_(sc_ctrl_) {}
  ScreenConnector() = delete;

 private:
  std::unique_ptr<ScreenConnectorSource> sc_android_impl_; // either socket_based or wayland
  bool is_frame_fetching_thread_started_;
  ScreenConnectorCtrl sc_ctrl_;
  ScreenConnectorQueue<ProcessedFrameType> sc_android_queue_;
  GenerateProcessedFrameCallback callback_from_streamer_;
  std::thread sc_android_impl_fetcher_;
  std::mutex streamer_callback_mutex_; // mutex to set & read callback_from_streamer_
};

}  // namespace cuttlefish

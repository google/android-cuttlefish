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

#include <cstdint>

#include "common/libs/concurrency/multiplexer.h"
#include "common/libs/confui/confui.h"

#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/screen_connector/screen_connector_queue.h"

namespace cuttlefish {
template <typename ProcessedFrameType>
class ScreenConnectorInputMultiplexer {
  using Queue = ScreenConnectorQueue<ProcessedFrameType>;
  using Multiplexer = Multiplexer<ProcessedFrameType, Queue>;

 public:
  ScreenConnectorInputMultiplexer(HostModeCtrl& host_mode_ctrl)
      : host_mode_ctrl_(host_mode_ctrl) {
    sc_android_queue_id_ =
        multiplexer_.RegisterQueue(multiplexer_.CreateQueue(/* q size */ 2));
    sc_confui_queue_id_ =
        multiplexer_.RegisterQueue(multiplexer_.CreateQueue(/* q size */ 2));
  }

  virtual ~ScreenConnectorInputMultiplexer() = default;

  void PushToAndroidQueue(ProcessedFrameType&& t) {
    multiplexer_.Push(sc_android_queue_id_, std::move(t));
  }

  void PushToConfUiQueue(ProcessedFrameType&& t) {
    multiplexer_.Push(sc_confui_queue_id_, std::move(t));
  }

  // customize Pop()
  ProcessedFrameType Pop() {
    on_next_frame_cnt_++;

    // is_discard_frame is thread-specific
    bool is_discard_frame = false;

    // callback to select the queue index, and update is_discard_frame
    auto selector = [this, &is_discard_frame]() -> int {
      if (multiplexer_.IsEmpty(sc_android_queue_id_)) {
        ConfUiLog(VERBOSE)
            << "Streamer gets Conf UI frame with host ctrl mode = "
            << static_cast<std::uint32_t>(host_mode_ctrl_.GetMode())
            << " and cnd = #" << on_next_frame_cnt_;
        return sc_confui_queue_id_;
      }
      auto mode = host_mode_ctrl_.GetMode();
      if (mode != HostModeCtrl::ModeType::kAndroidMode) {
        // AndroidFrameFetchingLoop could have added 1 or 2 frames
        // before it becomes Conf UI mode.
        ConfUiLog(VERBOSE)
            << "Streamer ignores Android frame with host ctrl mode ="
            << static_cast<std::uint32_t>(mode) << "and cnd = #"
            << on_next_frame_cnt_;
        is_discard_frame = true;
      }
      ConfUiLog(VERBOSE) << "Streamer gets Android frame with host ctrl mode ="
                         << static_cast<std::uint32_t>(mode) << "and cnd = #"
                         << on_next_frame_cnt_;
      return sc_android_queue_id_;
    };

    while (true) {
      ConfUiLog(VERBOSE) << "Streamer waiting Semaphore with host ctrl mode ="
                         << static_cast<std::uint32_t>(
                                host_mode_ctrl_.GetMode())
                         << " and cnd = #" << on_next_frame_cnt_;
      auto processed_frame = multiplexer_.Pop(selector);
      if (!is_discard_frame) {
        return processed_frame;
      }
      is_discard_frame = false;
    }
  }

 private:
  HostModeCtrl& host_mode_ctrl_;
  Multiplexer multiplexer_;
  unsigned long long int on_next_frame_cnt_;
  int sc_android_queue_id_;
  int sc_confui_queue_id_;
};
}  // end of namespace cuttlefish

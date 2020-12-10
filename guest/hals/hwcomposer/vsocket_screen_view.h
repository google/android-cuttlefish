/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "guest/hals/hwcomposer/screen_view.h"

namespace cuttlefish {

class VsocketScreenView : public ScreenView {
 public:
  VsocketScreenView();
  virtual ~VsocketScreenView();

  std::uint8_t* AcquireNextBuffer(std::uint32_t display_number) override;

  void PresentAcquiredBuffer(std::uint32_t display_number) override;

 private:
  bool ConnectToScreenServer();

  void BroadcastLoop();

  void ClientDetectorLoop();

  class DisplayHelper {
   public:
    DisplayHelper(std::uint32_t display_number);

    DisplayHelper(const DisplayHelper&) = delete;
    DisplayHelper& operator=(const DisplayHelper&) = delete;

    DisplayHelper(DisplayHelper&&) = delete;
    DisplayHelper& operator=(DisplayHelper&&) = delete;

    std::uint8_t* AcquireNextBuffer();

    void PresentAcquiredBuffer();

    // Returns true if this display has a new frame ready to be sent.
    bool HasPresentBuffer();

    bool SendPresentBufferIfAvailable(cuttlefish::SharedFD* connection);

   private:
    std::uint8_t* GetBuffer(std::uint32_t index);

    static constexpr std::uint32_t kNumBuffersPerDisplay = 8;

    std::uint32_t display_number_ = 0;

    std::size_t buffer_size_ = 0;
    std::vector<std::uint8_t> buffers_;

    std::mutex acquire_mutex_;
    std::deque<std::uint32_t> acquirable_buffers_indexes_;
    std::optional<std::uint32_t> acquired_buffer_index_;

    std::mutex present_mutex_;
    std::optional<std::uint32_t> present_buffer_index_;
  };

  std::vector<std::unique_ptr<DisplayHelper>> display_helpers_;

  cuttlefish::SharedFD screen_server_;
  std::thread broadcast_thread_;
  std::thread client_detector_thread_;
  bool send_frames_ = false;
  std::mutex mutex_;
  std::condition_variable cond_var_;
  bool running_ = true;
};

}  // namespace cuttlefish
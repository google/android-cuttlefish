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

#include "host/libs/screen_connector/screen_connector.h"

#include <atomic>
#include <cinttypes>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

class SocketBasedScreenConnector : public ScreenConnector {
 public:
  explicit SocketBasedScreenConnector(int frames_fd);

  bool OnNextFrame(const FrameCallback& frame_callback) override;

  void ReportClientsConnected(bool have_clients) override;

 private:
  void ServerLoop(int frames_fd);

  class DisplayHelper {
   public:
    DisplayHelper(std::uint32_t display_number);

    DisplayHelper(const DisplayHelper&) = delete;
    DisplayHelper& operator=(const DisplayHelper&) = delete;

    DisplayHelper(DisplayHelper&&) = delete;
    DisplayHelper& operator=(DisplayHelper&&) = delete;

    std::uint8_t* AcquireNextBuffer();

    void PresentAcquiredBuffer();

    bool ConsumePresentBuffer(const FrameCallback& frame_callback);

   private:
    std::uint8_t* GetBuffer(std::uint32_t index);

    static constexpr std::uint32_t kNumBuffersPerDisplay = 4;

    std::uint32_t display_number_ = 0;

    std::size_t buffer_size_ = 0;
    std::vector<std::uint8_t> buffers_;

    std::mutex acquire_mutex_;
    std::deque<std::uint32_t> acquirable_buffers_indexes_;
    std::optional<std::uint32_t> acquired_buffer_index_;

    std::mutex present_mutex_;
    std::optional<std::uint32_t> present_buffer_index_;
  };

  std::thread screen_server_thread_;
  cuttlefish::SharedFD client_connection_;
  bool have_clients_ = false;
  std::vector<std::unique_ptr<DisplayHelper>> display_helpers_;

  std::mutex frame_available_mutex_;
  std::condition_variable frame_available_cond_var_;
  std::size_t frame_available_display_index = 0;
};

} // namespace cuttlefish

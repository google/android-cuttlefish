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

#include "host/frontend/vnc_server/screen_connector.h"

#include <atomic>
#include <condition_variable>

#include <gflags/gflags.h>

#include <common/vsoc/lib/screen_region_view.h>
#include <host/libs/config/cuttlefish_config.h>
#include "host/frontend/vnc_server/vnc_utils.h"

DEFINE_int32(frame_server_fd, -1, "");

namespace cvd {
namespace vnc {

namespace {
class VSoCScreenConnector : public ScreenConnector {
 public:
  int WaitForNewFrameSince(std::uint32_t* seq_num) override {
    if (!screen_view_) return -1;
    return screen_view_->WaitForNewFrameSince(seq_num);
  }

  void* GetBuffer(int buffer_idx) override {
    if (!screen_view_) return nullptr;
    return screen_view_->GetBuffer(buffer_idx);
  }

 private:
  vsoc::screen::ScreenRegionView* screen_view_ =
      vsoc::screen::ScreenRegionView::GetInstance(vsoc::GetDomain().c_str());
};

// TODO(b/128852363): Substitute with one based on memory shared with the
//  wayland mock
class SocketBasedScreenConnector : public ScreenConnector {
 public:
  SocketBasedScreenConnector() {
    screen_server_thread_ = std::thread([this]() { ServerLoop(); });
  }

  int WaitForNewFrameSince(std::uint32_t* seq_num) override {
    std::unique_lock<std::mutex> lock(new_frame_mtx_);
    while (seq_num_ == *seq_num) {
      new_frame_cond_var_.wait(lock);
    }
    *seq_num = seq_num_;
    return newest_buffer_;
  }

  void* GetBuffer(int buffer_idx) override {
    if (buffer_idx < 0) return nullptr;
    buffer_idx %= NUM_BUFFERS_;
    return &buffer_[buffer_idx * ScreenSizeInBytes()];
  }

 private:
  static constexpr int NUM_BUFFERS_ = 4;

  void ServerLoop() {
    if (FLAGS_frame_server_fd < 0) {
      LOG(FATAL) << "Invalid file descriptor: " << FLAGS_frame_server_fd;
      return;
    }
    auto server = SharedFD::Dup(FLAGS_frame_server_fd);
    close(FLAGS_frame_server_fd);
    if (!server->IsOpen()) {
      LOG(FATAL) << "Unable to dup screen server: " << server->StrError();
      return;
    }

    int current_buffer = 0;

    while (1) {
      LOG(INFO) << "Screen Connector accepting connections...";
      auto conn = SharedFD::Accept(*server);
      if (!conn->IsOpen()) {
        LOG(ERROR) << "Disconnected fd returned from accept";
        continue;
      }
      while (conn->IsOpen()) {
        int32_t size = 0;
        if (conn->Read(&size, sizeof(size)) < 0) {
          LOG(ERROR) << "Failed to read from hwcomposer: "
                      << conn->StrError();
          break;
        }
        auto buff = reinterpret_cast<uint8_t*>(GetBuffer(current_buffer));
        while (size > 0) {
          auto read = conn->Read(buff, size);
          if (read < 0) {
            LOG(ERROR) << "Failed to read from hwcomposer: "
                       << conn->StrError();
            conn->Close();
            break;
          }
          size -= read;
          buff += read;
        }
        BroadcastNewFrame(current_buffer);
        current_buffer = (current_buffer + 1) % NUM_BUFFERS_;
      }
    }
  }

  void BroadcastNewFrame(int buffer_idx) {
    {
      std::lock_guard<std::mutex> lock(new_frame_mtx_);
      seq_num_++;
      newest_buffer_ = buffer_idx;
    }
    new_frame_cond_var_.notify_all();
  }

  std::vector<std::uint8_t> buffer_ =
      std::vector<std::uint8_t>(NUM_BUFFERS_ * ScreenSizeInBytes());
  std::uint32_t seq_num_{0};
  int newest_buffer_ = 0;
  std::condition_variable new_frame_cond_var_;
  std::mutex new_frame_mtx_;
  std::thread screen_server_thread_;
};
}  // namespace

ScreenConnector* ScreenConnector::Get() {
  auto config = vsoc::CuttlefishConfig::Get();
  if (config->enable_ivserver()) {
    return new VSoCScreenConnector();
  } else {
    return new SocketBasedScreenConnector();
  }
}

}  // namespace vnc
}  // namespace cvd

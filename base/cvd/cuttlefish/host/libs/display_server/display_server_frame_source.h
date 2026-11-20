/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/display_server/protocol.h"
#include "cuttlefish/host/libs/screen_connector/frame_source.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish::display_server {

// A FrameSource that receives frames from a running cvd_display_server.
class DisplayServerFrameSource : public FrameSource {
 public:
  static Result<std::unique_ptr<DisplayServerFrameSource>> Create(
      const std::string& socket_path);
  ~DisplayServerFrameSource() override;

  void SetFrameCallback(FrameCallback callback) override;
  void SetDisplayEventCallback(DisplayEventCallback callback) override;

 private:
  struct MappedDisplay {
    uint32_t width = 0;
    uint32_t height = 0;
    size_t slot_size = 0;
    FrameInfo frame_info = {};
    ScopedMMap shm;
    std::optional<uint32_t> last_slot;
  };

  struct Frame {
    FrameInfo info;
    uint8_t* pixels;
  };

  DisplayServerFrameSource(SharedFD socket, SharedFD stop_event);
  void ReceiveLoop();
  Result<void> HandleMessage(const void* data, size_t size);
  void OnDisplayCreated(const DisplayMsg& msg);
  void OnDisplayDestroyed(const DisplayMsg& msg);
  Result<void> OnDisplayShm(const DisplayMsg& msg);
  Result<void> OnFrame(const FrameMsg& msg);
  Result<void> AttachShm(const DisplayMsg& msg);
  static Result<Frame> GetFrameFromShm(MappedDisplay& display, uint32_t slot);

  SharedFD socket_;
  SharedFD stop_event_;
  std::mutex frame_delivery_mutex_;
  std::mutex callback_mutex_;
  FrameCallback frame_callback_;
  DisplayEventCallback display_event_callback_;
  std::map<uint32_t, MappedDisplay> displays_;
  std::thread receive_thread_;
};

}  // namespace cuttlefish::display_server

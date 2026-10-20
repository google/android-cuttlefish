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

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/display_server/protocol.h"
#include "cuttlefish/host/libs/screen_connector/frame_source.h"
#include "cuttlefish/host/libs/screen_connector/screen_connector_common.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"

namespace cuttlefish::display_server {

// A FrameSource that receives frames from a running cvd_display_server.
class DisplayServerFrameSource : public FrameSource {
 public:
  explicit DisplayServerFrameSource(const std::string& socket_path);
  ~DisplayServerFrameSource() override;

  void SetFrameCallback(GenerateProcessedFrameCallbackImpl callback) override;
  void SetDisplayEventCallback(DisplayEventCallback callback) override;

 private:
  struct MappedDisplay {
    uint32_t width = 0;
    uint32_t height = 0;
    std::string shm_name;
    size_t slot_size = 0;
    ScopedMMap shm;
  };

  void ReceiveLoop();
  void HandleMessage(const DisplayMsg& msg);
  void OnDisplayCreated(const DisplayMsg& msg);
  void OnDisplayDestroyed(const DisplayMsg& msg);
  void OnFrame(const DisplayMsg& msg);
  bool AttachShm(uint32_t display_number, const DisplayMsg& msg);

  SharedFD socket_;
  std::atomic<bool> running_{true};
  std::mutex callback_mutex_;
  GenerateProcessedFrameCallbackImpl frame_callback_;
  DisplayEventCallback display_event_callback_;
  std::map<uint32_t, MappedDisplay> displays_;
  std::thread receive_thread_;
};

}  // namespace cuttlefish::display_server

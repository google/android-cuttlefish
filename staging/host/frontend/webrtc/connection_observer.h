/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <map>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/frontend/webrtc/display_handler.h"
#include "host/frontend/webrtc/kernel_log_events_handler.h"
#include "host/frontend/webrtc/libdevice/camera_controller.h"
#include "host/frontend/webrtc/libdevice/connection_observer.h"
#include "host/libs/confui/host_virtual_input.h"

namespace cuttlefish {

struct InputSockets {
  SharedFD GetTouchClientByLabel(const std::string& label) {
    return touch_clients[label];
  }

  // TODO (b/186773052): Finding strings in a map for every input event may
  // introduce unwanted latency.
  std::map<std::string, SharedFD> touch_servers;
  std::map<std::string, SharedFD> touch_clients;
  SharedFD keyboard_server;
  SharedFD keyboard_client;
  SharedFD switches_server;
  SharedFD switches_client;
};

class CfConnectionObserverFactory
    : public webrtc_streaming::ConnectionObserverFactory {
 public:
  CfConnectionObserverFactory(
      cuttlefish::InputSockets& input_sockets,
      KernelLogEventsHandler* kernel_log_events_handler,
      cuttlefish::confui::HostVirtualInput& confui_input);
  ~CfConnectionObserverFactory() override = default;

  std::shared_ptr<webrtc_streaming::ConnectionObserver> CreateObserver()
      override;

  void AddCustomActionServer(SharedFD custom_action_server_fd,
                             const std::vector<std::string>& commands);

  void SetDisplayHandler(std::weak_ptr<DisplayHandler> display_handler);

  void SetCameraHandler(CameraController* controller);

 private:
  InputSockets& input_sockets_;
  KernelLogEventsHandler* kernel_log_events_handler_;
  std::map<std::string, SharedFD>
      commands_to_custom_action_servers_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
  cuttlefish::confui::HostVirtualInput& confui_input_;
  cuttlefish::CameraController* camera_controller_ = nullptr;
};

}  // namespace cuttlefish

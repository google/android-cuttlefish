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
#include "host/frontend/webrtc/lib/connection_observer.h"

namespace cuttlefish {

struct InputSockets {
  cuttlefish::SharedFD touch_server;
  cuttlefish::SharedFD touch_client;
  cuttlefish::SharedFD keyboard_server;
  cuttlefish::SharedFD keyboard_client;
  cuttlefish::SharedFD switches_server;
  cuttlefish::SharedFD switches_client;
};

class CfConnectionObserverFactory
    : public cuttlefish::webrtc_streaming::ConnectionObserverFactory {
 public:
  CfConnectionObserverFactory(cuttlefish::InputSockets& input_sockets,
                              cuttlefish::SharedFD kernel_log_events_fd);
  ~CfConnectionObserverFactory() override = default;

  std::shared_ptr<cuttlefish::webrtc_streaming::ConnectionObserver> CreateObserver()
      override;

  void AddCustomActionServer(cuttlefish::SharedFD custom_action_server_fd,
                             const std::vector<std::string>& commands);

  void SetDisplayHandler(std::weak_ptr<DisplayHandler> display_handler);

 private:
  cuttlefish::InputSockets& input_sockets_;
  cuttlefish::SharedFD kernel_log_events_fd_;
  std::map<std::string, cuttlefish::SharedFD>
      commands_to_custom_action_servers_;
  std::weak_ptr<DisplayHandler> weak_display_handler_;
};

}  // namespace cuttlefish

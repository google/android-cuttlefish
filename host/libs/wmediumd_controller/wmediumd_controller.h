//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/wmediumd_controller/wmediumd_api_protocol.h"

namespace cuttlefish {

class WmediumdController {
 public:
  static std::unique_ptr<WmediumdController> New(
      const std::string& serverSocketPath);

  virtual ~WmediumdController() {}

  WmediumdController(const WmediumdController& rhs) = delete;
  WmediumdController& operator=(const WmediumdController& rhs) = delete;

  WmediumdController(WmediumdController&& rhs) = delete;
  WmediumdController& operator=(WmediumdController&& rhs) = delete;

  bool SetControl(const uint32_t flags);
  bool SetSnr(const std::string& node1, const std::string& node2, uint8_t snr);
  bool ReloadCurrentConfig(void);
  bool ReloadConfig(const std::string& configPath);
  bool StartPcap(const std::string& pcapPath);
  bool StopPcap(void);
  std::optional<WmediumdMessageStationsList> GetStations(void);
  bool SetPosition(const std::string& node, double x, double y);
  bool SetLci(const std::string& node, const std::string& lci);
  bool SetCivicloc(const std::string& node, const std::string& civicloc);

 private:
  WmediumdController() {}

  bool Connect(const std::string& serverSocketPath);
  bool SendMessage(const WmediumdMessage& message);
  std::optional<WmediumdMessageReply> SendMessageWithReply(
      const WmediumdMessage& message);

  SharedFD wmediumd_socket_;
};

}  // namespace cuttlefish

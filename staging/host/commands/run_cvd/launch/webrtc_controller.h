//
// Copyright (C) 2023 The Android Open Source Project
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

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/feature.h"

#include "host/frontend/webrtc/webrtc_command_channel.h"
#include "webrtc_commands.pb.h"

namespace cuttlefish {

class WebRtcController : public SetupFeature {
 public:
  INJECT(WebRtcController()) {};
  std::string Name() const override { return "WebRtcController"; }
  Result<void> ResultSetup() override;

  SharedFD GetClientSocket() const;
  Result<void> SendStartRecordingCommand();
  Result<void> SendStopRecordingCommand();
  Result<void> SendScreenshotDisplayCommand(int display_number,
                                            const std::string& screenshot_path);

 protected:
  SharedFD client_socket_;
  std::optional<WebrtcClientCommandChannel> command_channel_;

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
};

fruit::Component<WebRtcController> WebRtcControllerComponent();

}  // namespace cuttlefish

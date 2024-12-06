/*
 * Copyright 2024 The Android Open Source Project
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

#include "common/libs/transport/channel_sharedfd.h"
#include "webrtc_commands.pb.h"

namespace cuttlefish {

class WebrtcClientCommandChannel {
 public:
  WebrtcClientCommandChannel(SharedFD fd);

  Result<webrtc::WebrtcCommandResponse> SendCommand(
      const webrtc::WebrtcCommandRequest& request);

 private:
  transport::SharedFdChannel channel_;
};

class WebrtcServerCommandChannel {
 public:
  WebrtcServerCommandChannel(SharedFD fd);

  Result<webrtc::WebrtcCommandRequest> ReceiveRequest();
  Result<void> SendResponse(const webrtc::WebrtcCommandResponse& response);

 private:
  transport::SharedFdChannel channel_;
};

}  // namespace cuttlefish
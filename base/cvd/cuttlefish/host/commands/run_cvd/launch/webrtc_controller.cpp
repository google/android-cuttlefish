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

#include "host/commands/run_cvd/launch/webrtc_controller.h"

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/transport/channel_sharedfd.h"
#include "common/libs/utils/result.h"
#include "google/rpc/code.pb.h"
#include "host/commands/run_cvd/launch/launch.h"
#include "webrtc_commands.pb.h"

namespace cuttlefish {
namespace {

Result<void> IsSuccess(const webrtc::WebrtcCommandResponse& response) {
  CF_EXPECT(response.has_status(), "Webrtc command response missing status?");
  const auto& response_status = response.status();
  CF_EXPECT_EQ(response_status.code(), google::rpc::Code::OK,
               "Webrtc command failed: " << response_status.message());
  return {};
}

}  // namespace

using webrtc::WebrtcCommandRequest;
using webrtc::WebrtcCommandResponse;

Result<void> WebRtcController::ResultSetup() {
  LOG(DEBUG) << "Initializing the WebRTC command sockets.";
  SharedFD host_socket;
  CF_EXPECT(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &client_socket_,
                                 &host_socket),
            client_socket_->StrError());

  command_channel_.emplace(host_socket);
  return {};
}

SharedFD WebRtcController::GetClientSocket() const { return client_socket_; }

Result<void> WebRtcController::SendStartRecordingCommand() {
  CF_EXPECT(command_channel_.has_value(), "Not initialized?");
  WebrtcCommandRequest request;
  request.mutable_start_recording_request();
  WebrtcCommandResponse response =
      CF_EXPECT(command_channel_->SendCommand(request));
  CF_EXPECT(IsSuccess(response), "Failed to start recording.");
  return {};
}

Result<void> WebRtcController::SendStopRecordingCommand() {
  CF_EXPECT(command_channel_.has_value(), "Not initialized?");
  WebrtcCommandRequest request;
  request.mutable_stop_recording_request();
  WebrtcCommandResponse response =
      CF_EXPECT(command_channel_->SendCommand(request));
  CF_EXPECT(IsSuccess(response), "Failed to stop recording.");
  return {};
}

Result<void> WebRtcController::SendScreenshotDisplayCommand(
    int display_number, const std::string& screenshot_path) {
  CF_EXPECT(command_channel_.has_value(), "Not initialized?");
  WebrtcCommandRequest request;
  auto* screenshot_request = request.mutable_screenshot_display_request();
  screenshot_request->set_display_number(display_number);
  screenshot_request->set_screenshot_path(screenshot_path);
  WebrtcCommandResponse response =
      CF_EXPECT(command_channel_->SendCommand(request));
  CF_EXPECT(IsSuccess(response), "Failed to screenshot display.");
  return {};
}

fruit::Component<WebRtcController> WebRtcControllerComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, WebRtcController>();
}

}  // namespace cuttlefish

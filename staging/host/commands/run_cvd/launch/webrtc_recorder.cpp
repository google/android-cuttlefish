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

#include "host/commands/run_cvd/launch/webrtc_recorder.h"

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/launch/launch.h"

namespace cuttlefish {

Result<void> WebRtcRecorder::ResultSetup() {
 LOG(DEBUG) << "Initializing the WebRTC recording sockets.";
 CF_EXPECT(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &client_socket_,
                                &host_socket_),
           client_socket_->StrError());
 struct timeval timeout;
 timeout.tv_sec = 3;
 timeout.tv_usec = 0;
 CHECK(host_socket_->SetSockOpt(SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                sizeof(timeout)) == 0)
     << "Could not set receive timeout";
 return {};
}

SharedFD WebRtcRecorder::GetClientSocket() const { return client_socket_;}

Result<void> WebRtcRecorder::SendStartRecordingCommand() const {
 CF_EXPECT(SendCommandAndVerifyResponse("T"));
 return {};
}

Result<void> WebRtcRecorder::SendStopRecordingCommand() const {
 CF_EXPECT(SendCommandAndVerifyResponse("C"));
 return {};
}

Result<void> WebRtcRecorder::SendCommandAndVerifyResponse(std::string message) const {
 CF_EXPECTF(WriteAll(host_socket_, message) == message.size(),
            "Failed to send message:  '{}'", message);
 char response[1];
 int read_ret = host_socket_->Read(response, sizeof(response));
 CF_EXPECT_NE(read_ret, 0,
              "Failed to read response from the recording manager.");
 CF_EXPECT_EQ(response[0], 'Y',
              "Did not receive expected success response from the recording "
              "manager.");
 return {};
}

fruit::Component<WebRtcRecorder> WebRtcRecorderComponent() {
  return fruit::createComponent().addMultibinding<SetupFeature, WebRtcRecorder>();
}

}  // namespace cuttlefish
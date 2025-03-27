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

#include "host/frontend/webrtc/webrtc_command_channel.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace {

constexpr const uint32_t kUnusedCommandField = 0;

template <typename ProtoT>
Result<transport::ManagedMessage> ToMessage(const ProtoT& proto) {
  std::string proto_str;
  CF_EXPECT(proto.SerializeToString(&proto_str), "Failed to serialize proto.");

  auto msg = CF_EXPECT(
      transport::CreateMessage(kUnusedCommandField, proto_str.size()));
  std::memcpy(msg->payload, proto_str.data(), proto_str.size());
  return msg;
}

template <typename ProtoT>
Result<ProtoT> ToProto(transport::ManagedMessage msg) {
  ProtoT proto;
  CF_EXPECT(proto.ParseFromArray(msg->payload, msg->payload_size),
            "Failed to serialize proto from message.");
  return proto;
}

}  // namespace

WebrtcClientCommandChannel::WebrtcClientCommandChannel(SharedFD fd)
    : channel_(fd, fd) {}

Result<webrtc::WebrtcCommandResponse> WebrtcClientCommandChannel::SendCommand(
    const webrtc::WebrtcCommandRequest& request) {
  auto request_msg = CF_EXPECT(
      ToMessage(request),
      "Failed to convert webrtc command request to transport message.");

  CF_EXPECT(channel_.SendRequest(*request_msg),
            "Failed to send webrtc command request.");

  CF_EXPECT(channel_.WaitForMessage(),
            "Failed to wait for webrtc command response.");

  auto response_msg = CF_EXPECT(channel_.ReceiveMessage(),
                                "Failed to receive webrtc command response.");

  return CF_EXPECT(
      ToProto<webrtc::WebrtcCommandResponse>(std::move(response_msg)));
}

Result<webrtc::WebrtcCommandRequest>
WebrtcServerCommandChannel::ReceiveRequest() {
  CF_EXPECT(channel_.WaitForMessage(),
            "Failed to wait for webrtc command response.");

  auto request_msg = CF_EXPECT(channel_.ReceiveMessage(),
                               "Failed to receive webrtc command request.");

  return CF_EXPECT(
      ToProto<webrtc::WebrtcCommandRequest>(std::move(request_msg)),
      "Failed to deserialize webrtc command request.");
}

WebrtcServerCommandChannel::WebrtcServerCommandChannel(SharedFD fd)
    : channel_(fd, fd) {}

Result<void> WebrtcServerCommandChannel::SendResponse(
    const webrtc::WebrtcCommandResponse& response) {
  auto response_msg = CF_EXPECT(
      ToMessage(response),
      "Failed to convert webrtc command response to transport message.");

  CF_EXPECT(channel_.SendRequest(*response_msg),
            "Failed to send webrtc command response.");

  return {};
}

}  // namespace cuttlefish
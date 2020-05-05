//
// Copyright (C) 2020 The Android Open Source Project
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

namespace cvd {
namespace webrtc_signaling {

constexpr auto kTypeField = "message_type";
constexpr auto kDeviceInfoField = "device_info";
constexpr auto kDeviceIdField = "device_id";
constexpr auto kClientIdField = "client_id";
constexpr auto kPayloadField = "payload";
constexpr auto kServersField = "ice_servers";
// These are defined in the IceServer dictionary
constexpr auto kUrlsField = "urls";
constexpr auto kUsernameField = "username";
constexpr auto kCredentialField = "credential";
constexpr auto kCredentialTypeField = "credentialType";

constexpr auto kRegisterType = "register";
constexpr auto kForwardType = "forward";
constexpr auto kConfigType = "config";
constexpr auto kConnectType = "connect";
constexpr auto kDeviceInfoType = "device_info";
constexpr auto kClientMessageType = "client_msg";
constexpr auto kDeviceMessageType = "device_msg";

}  // namespace webrtc_signaling
}  // namespace cvd

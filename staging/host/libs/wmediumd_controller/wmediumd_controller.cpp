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

#include "wmediumd_controller.h"

#include <android-base/logging.h>
#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "common/libs/fs/shared_buf.h"

#include "host/libs/wmediumd_controller/wmediumd_api_protocol.h"

namespace cuttlefish {

std::unique_ptr<WmediumdController> WmediumdController::New(
    const std::string& serverSocketPath) {
  std::unique_ptr<WmediumdController> result(new WmediumdController);

  if (!result->Connect(serverSocketPath)) {
    return nullptr;
  }

  return result;
}

bool WmediumdController::Connect(const std::string& serverSocketPath) {
  wmediumd_socket_ =
      SharedFD::SocketLocalClient(serverSocketPath, false, SOCK_STREAM);

  if (!wmediumd_socket_->IsOpen()) {
    LOG(ERROR) << "Cannot connect wmediumd control socket " << serverSocketPath
               << ": " << wmediumd_socket_->StrError();
    return false;
  }

  return SetControl(0);
}

bool WmediumdController::SetSnr(const std::string& node1,
                                const std::string& node2, uint8_t snr) {
  return SendMessage(WmediumdMessageSetSnr(node1, node2, snr));
}

bool WmediumdController::SetControl(const uint32_t flags) {
  return SendMessage(WmediumdMessageSetControl(flags));
}

bool WmediumdController::ReloadCurrentConfig(void) {
  return SendMessage(WmediumdMessageReloadCurrentConfig());
}

bool WmediumdController::ReloadConfig(const std::string& configPath) {
  return SendMessage(WmediumdMessageReloadConfig(configPath));
}

bool WmediumdController::StartPcap(const std::string& pcapPath) {
  return SendMessage(WmediumdMessageStartPcap(pcapPath));
}

bool WmediumdController::StopPcap(void) {
  return SendMessage(WmediumdMessageStopPcap());
}

std::optional<WmediumdMessageStationsList> WmediumdController::GetStations(
    void) {
  auto reply = SendMessageWithReply(WmediumdMessageGetStations());

  if (!reply) {
    return std::nullopt;
  }

  return WmediumdMessageStationsList::Parse(*reply);
}

bool WmediumdController::SetPosition(const std::string& node, double x,
                                     double y) {
  return SendMessage(WmediumdMessageSetPosition(node, x, y));
}

bool WmediumdController::SetLci(const std::string& node,
                                const std::string& lci) {
  return SendMessage(WmediumdMessageSetLci(node, lci));
}

bool WmediumdController::SetCivicloc(const std::string& node,
                                     const std::string& civicloc) {
  return SendMessage(WmediumdMessageSetCivicloc(node, civicloc));
}

bool WmediumdController::SendMessage(const WmediumdMessage& message) {
  auto reply = SendMessageWithReply(message);

  if (!reply) {
    return false;
  }

  if (reply->Type() != WmediumdMessageType::kAck) {
    return false;
  }

  return true;
}

std::optional<WmediumdMessageReply> WmediumdController::SendMessageWithReply(
    const WmediumdMessage& message) {
  auto sendResult = SendAll(wmediumd_socket_, message.Serialize());

  if (!sendResult) {
    LOG(ERROR) << "sendmessage failed: " << wmediumd_socket_->StrError();
    return std::nullopt;
  }

  std::string recvHeader = RecvAll(wmediumd_socket_, sizeof(uint32_t) * 2);

  if (recvHeader.size() != sizeof(uint32_t) * 2) {
    LOG(ERROR)
        << "error: RecvAll failed while receiving result header from server";
    return std::nullopt;
  }

  uint32_t type = *reinterpret_cast<const uint32_t*>(recvHeader.c_str());
  uint32_t dataLen =
      *reinterpret_cast<const uint32_t*>(recvHeader.c_str() + sizeof(uint32_t));

  std::string recvData = RecvAll(wmediumd_socket_, dataLen);

  if (recvData.size() != dataLen) {
    LOG(ERROR)
        << "error: RecvAll failed while receiving result data from server";
    return std::nullopt;
  }

  return WmediumdMessageReply(static_cast<WmediumdMessageType>(type), recvData);
}

}  // namespace cuttlefish

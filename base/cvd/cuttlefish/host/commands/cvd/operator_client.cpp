/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/operator_client.h"

#include <sys/socket.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <json/json.h>
#include <fmt/format.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"
#include "host/commands/cvd/selector/instance_group_record.h"

namespace cuttlefish {

namespace {
Result<Json::Value> BuildPregistrationMsg(
    const selector::LocalInstanceGroup& group) {
  Json::Value msg;
  msg["message_type"] = "pre-register";
  msg["group_name"] = group.GroupName();
  msg["owner"] = CF_EXPECT(CurrentUserName());
  Json::Value devices(Json::arrayValue);
  for (const auto& instance : group.Instances()) {
    Json::Value dev;
    dev["id"] = instance.webrtc_device_id();
    dev["name"] = instance.name();
    dev["adb_port"] = selector::AdbPort(instance);
    devices.append(dev);
  }
  msg["devices"] = devices;
  return msg;
}

Result<void> SendMsg(SharedFD fd, const Json::Value& msg) {
  std::string buf = msg.toStyledString();
  CF_EXPECT_EQ(WriteAll(fd, buf), (ssize_t)buf.size(),
               "Failed to send message: " << fd->StrError());
  return {};
}

Result<Json::Value> RecvMsg(SharedFD fd) {
  std::vector<char> buf(4096, '\0');
  auto read = fd->Read(buf.data(), buf.size());
  CF_EXPECTF(read >= 0, "Failed to receive message: {}", fd->StrError());
  CF_EXPECT(read != 0, "The operator closed the connection without responding");
  return CF_EXPECT(ParseJson(std::string_view(buf.data(), read)),
                   "Failed to parse operator response");
}
}  // namespace

Result<std::unique_ptr<OperatorControlConn>> OperatorControlConn::Create(
    const std::string& socket_path) {
  auto fd = SharedFD::SocketLocalClient(socket_path, false, SOCK_SEQPACKET);
  CF_EXPECTF(fd->IsOpen(), "Failed to connect to control socket: {}",
             fd->StrError());
  return std::unique_ptr<OperatorControlConn>(new OperatorControlConn(fd));
}

/**
 * Pre-registers an instance group with the operator
 */
Result<void> OperatorControlConn::Preregister(
    const selector::LocalInstanceGroup& group) {
  CF_EXPECT(SendMsg(conn_, CF_EXPECT(BuildPregistrationMsg(group))),
            "Failed to send pre-registration message to operator");
  Json::Value response =
      CF_EXPECT(RecvMsg(conn_), "Error receiving pre-registration response");

  std::stringstream error_messages;
  bool errors_found = false;
  for (const auto& res : CF_EXPECT(GetArrayValues<Json::Value>(response, {}))) {
    auto id = CF_EXPECT(GetValue<std::string>(res, {"id"}));
    auto status = CF_EXPECT(GetValue<std::string>(res, {"status"}));
    auto message = CF_EXPECT(GetValue<std::string>(res, {"message"}));
    if (status != "accepted") {
      fmt::print(error_messages, "\nid: {}, status: {}, message: {}", id,
                 status, message);
      errors_found = true;
    }
  }
  CF_EXPECTF(!errors_found,
             "Operator reported error pre-registering instances: {}",
             error_messages.str());
  return {};
}
}  // namespace cuttlefish

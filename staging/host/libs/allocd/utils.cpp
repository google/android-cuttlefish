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

#include "host/libs/allocd/utils.h"

#include <cstdint>
#include <optional>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_buf.h"
#include "host/libs/allocd/request.h"

namespace cuttlefish {

constexpr uint16_t kCurHeaderVersion = 1;
constexpr uint16_t kMinHeaderVersion = 1;

bool SendJsonMsg(SharedFD client_socket, const Json::Value& resp) {
  LOG(INFO) << "Sending JSON message";
  Json::FastWriter writer;
  auto resp_str = writer.write(resp);

  std::string header_buff(sizeof(RequestHeader), 0);

  // fill in header
  RequestHeader* header = reinterpret_cast<RequestHeader*>(header_buff.data());
  header->len = resp_str.size();
  header->version = kCurHeaderVersion;

  auto payload = header_buff + resp_str;

  return SendAll(client_socket, payload);
}

std::optional<Json::Value> RecvJsonMsg(SharedFD client_socket) {
  LOG(INFO) << "Receiving JSON message";
  RequestHeader header;
  client_socket->Recv(&header, sizeof(header), recv_flags);

  if (header.version < kMinHeaderVersion) {
    LOG(WARNING) << "bad request header version: " << header.version;
    return std::nullopt;
  }

  std::string payload = RecvAll(client_socket, header.len);

  JsonRequestReader reader;
  return reader.parse(payload);
}

}  // namespace cuttlefish

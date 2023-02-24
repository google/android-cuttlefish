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

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/libs/allocd/request.h"

namespace cuttlefish {

// While the JSON schema and payload structure are designed to be extensible,
// and avoid version incompatibility. However, should project requirements
// change, it is necessary that we have a mechanism to handle incompatibilities
// that arise over time. If an incompatibility should come about, the
// kMinHeaderVersion constant should be increased to match the new minimal set
// of features that are supported.

/// Current supported Header version number
constexpr uint16_t kCurHeaderVersion = 1;

/// Oldest compatible header version number
constexpr uint16_t kMinHeaderVersion = 1;

const std::map<RequestType, const char*> RequestTyToStrMap = {
    {RequestType::ID, "alloc_id"},
    {RequestType::CreateInterface, "create_interface"},
    {RequestType::DestroyInterface, "destroy_interface"},
    {RequestType::StopSession, "stop_session"},
    {RequestType::Shutdown, "shutdown"},
    {RequestType::Invalid, "invalid"}};

const std::map<std::string, RequestType> StrToRequestTyMap = {
    {"alloc_id", RequestType::ID},
    {"create_interface", RequestType::CreateInterface},
    {"destroy_interface", RequestType::DestroyInterface},
    {"stop_session", RequestType::StopSession},
    {"shutdown", RequestType::Shutdown},
    {"invalid", RequestType::Invalid}};

const std::map<std::string, IfaceType> StrToIfaceTyMap = {
    {"invalid", IfaceType::Invalid}, {"mtap", IfaceType::mtap},
    {"wtap", IfaceType::wtap},       {"wifiap", IfaceType::wifiap},
    {"etap", IfaceType::etap},       {"wbr", IfaceType::wbr},
    {"ebr", IfaceType::ebr}};

const std::map<IfaceType, std::string> IfaceTyToStrMap = {
    {IfaceType::Invalid, "invalid"}, {IfaceType::mtap, "mtap"},
    {IfaceType::wtap, "wtap"},       {IfaceType::wifiap, "wifiap"},
    {IfaceType::etap, "etap"},       {IfaceType::wbr, "wbr"},
    {IfaceType::ebr, "ebr"}};

const std::map<RequestStatus, std::string> ReqStatusToStrMap = {
    {RequestStatus::Invalid, "invalid"},
    {RequestStatus::Pending, "pending"},
    {RequestStatus::Failure, "failure"},
    {RequestStatus::Success, "success"}};

const std::map<std::string, RequestStatus> StrToReqStatusMap = {
    {"invalid", RequestStatus::Invalid},
    {"pending", RequestStatus::Pending},
    {"failure", RequestStatus::Failure},
    {"success", RequestStatus::Success}};

bool SendJsonMsg(SharedFD client_socket, const Json::Value& resp) {
  LOG(INFO) << "Sending JSON message";
  Json::StreamWriterBuilder factory;
  auto resp_str = Json::writeString(factory, resp);

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
  client_socket->Recv(&header, sizeof(header), kRecvFlags);

  if (header.version < kMinHeaderVersion) {
    LOG(WARNING) << "bad request header version: " << header.version;
    return std::nullopt;
  }

  std::string payload = RecvAll(client_socket, header.len);

  JsonRequestReader reader;
  return reader.parse(payload);
}

std::string ReqTyToStr(RequestType req_ty) {
  switch (req_ty) {
    case RequestType::Invalid:
      return "invalid";
    case RequestType::Shutdown:
      return "shutdown";
    case RequestType::StopSession:
      return "stop_session";
    case RequestType::DestroyInterface:
      return "destroy_interface";
    case RequestType::CreateInterface:
      return "create_interface";
    case RequestType::ID:
      return "id";
  }
}

RequestType StrToReqTy(const std::string& req) {
  auto it = StrToRequestTyMap.find(req);
  if (it == StrToRequestTyMap.end()) {
    return RequestType::Invalid;
  } else {
    return it->second;
  }
}

RequestStatus StrToStatus(const std::string& st) {
  auto it = StrToReqStatusMap.find(st);
  if (it == StrToReqStatusMap.end()) {
    return RequestStatus::Invalid;
  } else {
    return it->second;
  }
}

std::string StatusToStr(RequestStatus st) {
  switch (st) {
    case RequestStatus::Invalid:
      return "invalid";
    case RequestStatus::Pending:
      return "pending";
    case RequestStatus::Success:
      return "success";
    case RequestStatus::Failure:
      return "failure";
  }
}

std::string IfaceTyToStr(IfaceType iface) {
  switch (iface) {
    case IfaceType::Invalid:
      return "invalid";
    case IfaceType::mtap:
      return "mtap";
    case IfaceType::wtap:
      return "wtap";
    case IfaceType::wifiap:
      return "wifiap";
    case IfaceType::etap:
      return "etap";
    case IfaceType::wbr:
      return "wbr";
    case IfaceType::ebr:
      return "ebr";
  }
}

IfaceType StrToIfaceTy(const std::string& iface) {
  auto it = StrToIfaceTyMap.find(iface);
  if (it == StrToIfaceTyMap.end()) {
    return IfaceType::Invalid;
  } else {
    return it->second;
  }
}

}  // namespace cuttlefish

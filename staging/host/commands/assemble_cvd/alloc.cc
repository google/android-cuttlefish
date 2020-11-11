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

#include "host/commands/assemble_cvd/alloc.h"

#include <iomanip>
#include <sstream>

#include "common/libs/fs/shared_fd.h"
#include "host/commands/assemble_cvd/assembler_defs.h"
#include "host/libs/allocd/request.h"
#include "host/libs/allocd/utils.h"

static std::string StrForInstance(const std::string& prefix, int num) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2) << num;
  return stream.str();
}

IfaceConfig DefaultNetworkInterfaces(int num) {
  IfaceConfig config{};
  config.mobile_tap.name = StrForInstance("cvd-mtap-", num);
  config.mobile_tap.resource_id = 0;
  config.mobile_tap.session_id = 0;

  config.wireless_tap.name = StrForInstance("cvd-wtap-", num);
  config.wireless_tap.resource_id = 0;
  config.wireless_tap.session_id = 0;

  config.ethernet_tap.name = StrForInstance("cvd-etap-", num);
  config.ethernet_tap.resource_id = 0;
  config.ethernet_tap.session_id = 0;

  return config;
}

std::optional<IfaceConfig> AllocateNetworkInterfaces() {
  IfaceConfig config{};

  cuttlefish::SharedFD allocd_sock = cuttlefish::SharedFD::SocketLocalClient(
      cuttlefish::kDefaultLocation, false, SOCK_STREAM);
  if (!allocd_sock->IsOpen()) {
    LOG(FATAL) << "Unable to connect to allocd on "
               << cuttlefish::kDefaultLocation << ": "
               << allocd_sock->StrError();
    exit(cuttlefish::kAllocdConnectionError);
  }

  Json::Value resource_config;
  Json::Value request_list;
  Json::Value req;
  req["request_type"] = "create_interface";
  req["uid"] = geteuid();
  req["iface_type"] = "mtap";
  request_list.append(req);
  req["iface_type"] = "wtap";
  request_list.append(req);
  req["iface_type"] = "etap";
  request_list.append(req);

  resource_config["config_request"]["request_list"] = request_list;

  if (!cuttlefish::SendJsonMsg(allocd_sock, resource_config)) {
    LOG(FATAL) << "Failed to send JSON to allocd\n";
    return std::nullopt;
  }

  auto resp_opt = cuttlefish::RecvJsonMsg(allocd_sock);
  if (!resp_opt.has_value()) {
    LOG(FATAL) << "Bad Response from allocd\n";
    exit(cuttlefish::kAllocdConnectionError);
  }
  auto resp = resp_opt.value();

  if (!resp.isMember("config_status") || !resp["config_status"].isString()) {
    LOG(FATAL) << "Bad response from allocd: " << resp;
    exit(cuttlefish::kAllocdConnectionError);
  }

  if (resp["config_status"].asString() !=
      cuttlefish::StatusToStr(cuttlefish::RequestStatus::Success)) {
    LOG(FATAL) << "Failed to allocate interfaces " << resp;
    exit(cuttlefish::kAllocdConnectionError);
  }

  if (!resp.isMember("session_id") || !resp["session_id"].isUInt()) {
    LOG(FATAL) << "Bad response from allocd: " << resp;
    exit(cuttlefish::kAllocdConnectionError);
  }
  auto session_id = resp["session_id"].asUInt();

  if (!resp.isMember("response_list") || !resp["response_list"].isArray()) {
    LOG(FATAL) << "Bad response from allocd: " << resp;
    exit(cuttlefish::kAllocdConnectionError);
  }

  Json::Value resp_list = resp["response_list"];
  Json::Value mtap_resp;
  Json::Value wtap_resp;
  Json::Value etap_resp;
  for (Json::Value::ArrayIndex i = 0; i != resp_list.size(); ++i) {
    auto ty = cuttlefish::StrToIfaceTy(resp_list[i]["iface_type"].asString());

    switch (ty) {
      case cuttlefish::IfaceType::mtap: {
        mtap_resp = resp_list[i];
        break;
      }
      case cuttlefish::IfaceType::wtap: {
        wtap_resp = resp_list[i];
        break;
      }
      case cuttlefish::IfaceType::etap: {
        etap_resp = resp_list[i];
        break;
      }
      default: {
        break;
      }
    }
  }

  if (!mtap_resp.isMember("iface_type")) {
    LOG(ERROR) << "Missing mtap response from allocd";
    return std::nullopt;
  }
  if (!wtap_resp.isMember("iface_type")) {
    LOG(ERROR) << "Missing wtap response from allocd";
    return std::nullopt;
  }
  if (!etap_resp.isMember("iface_type")) {
    LOG(ERROR) << "Missing etap response from allocd";
    return std::nullopt;
  }

  config.mobile_tap.name = mtap_resp["iface_name"].asString();
  config.mobile_tap.resource_id = mtap_resp["resource_id"].asUInt();
  config.mobile_tap.session_id = session_id;

  config.wireless_tap.name = wtap_resp["iface_name"].asString();
  config.wireless_tap.resource_id = wtap_resp["resource_id"].asUInt();
  config.wireless_tap.session_id = session_id;

  config.ethernet_tap.name = etap_resp["iface_name"].asString();
  config.ethernet_tap.resource_id = etap_resp["resource_id"].asUInt();
  config.ethernet_tap.session_id = session_id;

  return config;
}


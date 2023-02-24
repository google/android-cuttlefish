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

#include "host/libs/allocd/resource_manager.h"

#include <android-base/logging.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/allocd/alloc_utils.h"
#include "host/libs/allocd/request.h"
#include "host/libs/allocd/utils.h"
#include "json/forwards.h"
#include "json/value.h"
#include "json/writer.h"

namespace cuttlefish {

uid_t GetUserIDFromSock(SharedFD client_socket);

ResourceManager::~ResourceManager() {
  bool success = true;
  for (auto& res : managed_sessions_) {
    success &= res.second->ReleaseAllResources();
  }

  Json::Value resp;
  resp["request_type"] = "shutdown";
  auto status = success ? RequestStatus::Success : RequestStatus::Failure;
  resp["request_status"] = StatusToStr(status);
  SendJsonMsg(shutdown_socket_, resp);
  LOG(INFO) << "Daemon Shutdown complete";
  unlink(location.c_str());
}

void ResourceManager::SetSocketLocation(const std::string& sock_name) {
  location = sock_name;
}

void ResourceManager::SetUseEbtablesLegacy(bool use_legacy) {
  use_ebtables_legacy_ = use_legacy;
}

uint32_t ResourceManager::AllocateResourceID() {
  return global_resource_id_.fetch_add(1, std::memory_order_relaxed);
}

uint32_t ResourceManager::AllocateSessionID() {
  return session_id_.fetch_add(1, std::memory_order_relaxed);
}

bool ResourceManager::AddInterface(const std::string& iface, IfaceType ty,
                                   uint32_t resource_id, uid_t uid) {
  bool allocatedIface = false;
  std::shared_ptr<StaticResource> res = nullptr;

  bool didInsert = active_interfaces_.insert(iface).second;
  if (didInsert) {
    const char* idp = iface.c_str() + (iface.size() - 3);
    int small_id = atoi(idp);
    switch (ty) {
      case IfaceType::wifiap:
        // TODO(seungjaeyoo) : Support AddInterface for wifiap
        break;
      case IfaceType::mtap:
        // TODO(seungjaeyoo) : Support AddInterface for mtap uses IP prefix
        // different from kMobileIp.
        res = std::make_shared<MobileIface>(iface, uid, small_id, resource_id,
                                            kMobileIp);
        allocatedIface = res->AcquireResource();
        pending_add_.insert({resource_id, res});
        break;
      case IfaceType::wtap: {
        auto w = std::make_shared<EthernetIface>(
            iface, uid, small_id, resource_id, "cvd-wbr", kWirelessIp);
        w->SetUseEbtablesLegacy(use_ebtables_legacy_);
        w->SetHasIpv4(use_ipv4_bridge_);
        w->SetHasIpv6(use_ipv6_bridge_);
        res = w;
        allocatedIface = res->AcquireResource();
        pending_add_.insert({resource_id, res});
        break;
      }
      case IfaceType::etap: {
        auto w = std::make_shared<EthernetIface>(iface, uid, small_id,
                                                 resource_id, "cvd-ebr",
                                                 kEthernetIp);
        w->SetUseEbtablesLegacy(use_ebtables_legacy_);
        w->SetHasIpv4(use_ipv4_bridge_);
        w->SetHasIpv6(use_ipv6_bridge_);
        res = w;
        allocatedIface = res->AcquireResource();
        pending_add_.insert({resource_id, res});
        break;
      }
      case IfaceType::wbr:
      case IfaceType::ebr:
        allocatedIface = CreateBridge(iface);
        break;
      case IfaceType::Invalid:
        break;
    }
  } else {
    LOG(WARNING) << "Interface already in use: " << iface;
  }

  if (didInsert && !allocatedIface) {
    LOG(WARNING) << "Failed to allocate interface: " << iface;
    active_interfaces_.erase(iface);
    auto it = pending_add_.find(resource_id);
    it->second->ReleaseResource();
    pending_add_.erase(it);
  }

  LOG(INFO) << "Finish CreateInterface Request";

  return allocatedIface;
}

bool ResourceManager::RemoveInterface(const std::string& iface, IfaceType ty) {
  bool isManagedIface = active_interfaces_.erase(iface) > 0;
  bool removedIface = false;
  if (isManagedIface) {
    switch (ty) {
      case IfaceType::wifiap:
        // TODO(seungjaeyoo) : Support RemoveInterface for wifiap
        break;
      case IfaceType::mtap: {
        // TODO(seungjaeyoo) : Support RemoveInterface for mtap uses IP prefix
        // different from kMobileIp.
        const char* idp = iface.c_str() + (iface.size() - 3);
        int id = atoi(idp);
        removedIface = DestroyMobileIface(iface, id, kMobileIp);
        break;
      }
      case IfaceType::wtap:
      case IfaceType::etap:
        removedIface = DestroyEthernetIface(
            iface, use_ipv4_bridge_, use_ipv6_bridge_, use_ebtables_legacy_);
        break;
      case IfaceType::wbr:
      case IfaceType::ebr:
        removedIface = DestroyBridge(iface);
        break;
      case IfaceType::Invalid:
        break;
    }

  } else {
    LOG(WARNING) << "Interface not managed: " << iface;
  }

  if (isManagedIface) {
    LOG(INFO) << "Removed interface: " << iface;
  } else {
    LOG(WARNING) << "Could not remove interface: " << iface;
  }

  return isManagedIface;
}

bool ResourceManager::ValidateRequestList(const Json::Value& config) {
  if (!config.isMember("request_list") || !config["request_list"].isArray()) {
    LOG(WARNING) << "Request has invalid 'request_list' field";
    return false;
  }

  auto request_list = config["request_list"];

  Json::ArrayIndex size = request_list.size();
  if (size == 0) {
    LOG(WARNING) << "Request has empty 'request_list' field";
    return false;
  }

  for (Json::ArrayIndex i = 0; i < size; ++i) {
    if (!ValidateRequest(request_list[i])) {
      return false;
    }
  }

  return true;
}

bool ResourceManager::ValidateConfigRequest(const Json::Value& config) {
  if (!config.isMember("config_request") ||
      !config["config_request"].isObject()) {
    LOG(WARNING) << "Request has invalid 'config_request' field";
    return false;
  }

  Json::Value config_request = config["config_request"];

  return ValidateRequestList(config_request);
}

bool ResourceManager::ValidateRequest(const Json::Value& request) {
  if (!request.isMember("request_type") ||
      !request["request_type"].isString() ||
      StrToReqTy(request["request_type"].asString()) == RequestType::Invalid) {
    LOG(WARNING) << "Request has invalid 'request_type' field";
    return false;
  }
  return true;
}

void ResourceManager::JsonServer() {
  LOG(INFO) << "Starting server on " << kDefaultLocation;
  auto server = SharedFD::SocketLocalServer(kDefaultLocation, false,
                                            SOCK_STREAM, kSocketMode);
  CHECK(server->IsOpen()) << "Could not start server at " << kDefaultLocation;
  LOG(INFO) << "Accepting client connections";

  while (true) {
    auto client_socket = SharedFD::Accept(*server);
    CHECK(client_socket->IsOpen()) << "Error creating client socket";

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    int err = client_socket->SetSockOpt(SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                        sizeof(timeout));
    if (err < 0) {
      LOG(WARNING) << "Could not set socket timeout";
      continue;
    }

    auto req_opt = RecvJsonMsg(client_socket);

    if (!req_opt) {
      LOG(WARNING) << "Invalid JSON Request, closing connection";
      continue;
    }

    Json::Value req = req_opt.value();

    if (!ValidateConfigRequest(req)) {
      continue;
    }

    Json::Value req_list = req["config_request"]["request_list"];

    Json::Value config_response;
    Json::Value response_list;
    Json::ArrayIndex req_list_size = req_list.size();

    // sentinel value, so we can populate the list of responses correctly
    // without trying to satisfy requests that will be aborted
    bool transaction_failed = false;

    for (Json::ArrayIndex i = 0; i < req_list_size; ++i) {
      LOG(INFO) << "Processing Request: " << i;
      auto req = req_list[i];
      auto req_ty_str = req["request_type"].asString();
      auto req_ty = StrToReqTy(req_ty_str);

      Json::Value response;
      if (transaction_failed) {
        response["request_type"] = req_ty_str;
        response["request_status"] = "pending";
        response["error"] = "";
        response_list.append(response);
        continue;
      }

      switch (req_ty) {
        case RequestType::ID: {
          response = JsonHandleIdRequest();
          break;
        }
        case RequestType::Shutdown: {
          if (i != 0 || req_list_size != 1) {
            response["request_type"] = req_ty_str;
            response["request_status"] = "failed";
            response["error"] =
                "Shutdown requests cannot be processed with other "
                "configuration requests";
            response_list.append(response);
            break;
          } else {
            response = JsonHandleShutdownRequest(client_socket);
            response_list.append(response);
            return;
          }
        }
        case RequestType::CreateInterface: {
          response = JsonHandleCreateInterfaceRequest(client_socket, req);
          break;
        }
        case RequestType::DestroyInterface: {
          response = JsonHandleDestroyInterfaceRequest(req);
          break;
        }
        case RequestType::StopSession: {
          response = JsonHandleStopSessionRequest(
              req, GetUserIDFromSock(client_socket));
          break;
        }
        case RequestType::Invalid: {
          LOG(WARNING) << "Invalid Request Type: " << req["request_type"];
          break;
        }
      }

      response_list.append(response);
      if (!(response["request_status"].asString() ==
            StatusToStr(RequestStatus::Success))) {
        LOG(INFO) << "Request failed:" << req;
        transaction_failed = true;
        continue;
      }
    }

    config_response["response_list"] = response_list;

    auto status =
        transaction_failed ? RequestStatus::Failure : RequestStatus::Success;
    config_response["config_status"] = StatusToStr(status);

    if (!transaction_failed) {
      auto session_id = AllocateSessionID();
      config_response["session_id"] = session_id;
      auto s = std::make_shared<Session>(session_id,
                                         GetUserIDFromSock(client_socket));

      // commit the resources
      s->Insert(pending_add_);
      pending_add_.clear();
      managed_sessions_.insert({session_id, s});
    } else {
      // be sure to release anything we've acquired if the transaction failed
      for (auto& droped_resource : pending_add_) {
        droped_resource.second->ReleaseResource();
      }
    }

    SendJsonMsg(client_socket, config_response);
    LOG(INFO) << "Closing connection to client";
    client_socket->Close();
  }
  server->Close();
}

uid_t GetUserIDFromSock(SharedFD client_socket) {
  struct ucred ucred {};
  socklen_t len = sizeof(struct ucred);

  if (-1 == client_socket->GetSockOpt(SOL_SOCKET, SO_PEERCRED, &ucred, &len)) {
    LOG(WARNING) << "Failed to get Socket Credentials";
    return -1;
  }

  return ucred.uid;
}

bool ResourceManager::CheckCredentials(SharedFD client_socket, uid_t uid) {
  uid_t sock_uid = GetUserIDFromSock(client_socket);

  if (sock_uid == -1) {
    LOG(WARNING) << "Invalid Socket UID: " << uid;
    return false;
  }

  if (uid != sock_uid) {
    LOG(WARNING) << "Message UID: " << uid
                 << " does not match socket's EUID: " << sock_uid;
    return false;
  }

  return true;
}

Json::Value ResourceManager::JsonHandleIdRequest() {
  Json::Value resp;
  resp["request_type"] = "allocate_id";
  resp["request_status"] = StatusToStr(RequestStatus::Success);
  resp["id"] = AllocateSessionID();
  return resp;
}

Json::Value ResourceManager::JsonHandleShutdownRequest(SharedFD client_socket) {
  LOG(INFO) << "Received Shutdown Request";
  shutdown_socket_ = client_socket;

  Json::Value resp;
  resp["request_type"] = "shutdown";
  resp["request_status"] = "pending";
  resp["error"] = "";

  return resp;
}

Json::Value ResourceManager::JsonHandleCreateInterfaceRequest(
    SharedFD client_socket, const Json::Value& request) {
  LOG(INFO) << "Received CreateInterface Request";

  Json::Value resp;
  resp["request_type"] = "create_interface";
  resp["iface_name"] = "";
  resp["request_status"] = StatusToStr(RequestStatus::Failure);
  resp["error"] = "unknown";

  if (!request.isMember("uid") || !request["uid"].isUInt()) {
    auto err_msg = "Input event doesn't have a valid 'uid' field";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  if (!request.isMember("iface_type") || !request["iface_type"].isString()) {
    auto err_msg = "Input event doesn't have a valid 'iface_type' field";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  auto uid = request["uid"].asUInt();

  if (!CheckCredentials(client_socket, uid)) {
    auto err_msg = "Credential check failed";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  auto user_opt = GetUserName(uid);

  bool addedIface = false;
  std::stringstream ss;
  if (!user_opt) {
    auto err_msg = "UserName could not be matched to UID";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  } else {
    auto iface_ty_name = request["iface_type"].asString();
    resp["iface_type"] = iface_ty_name;
    auto iface_type = StrToIfaceTy(iface_ty_name);
    auto attempts = kMaxIfaceNameId;
    do {
      auto id = AllocateResourceID();
      resp["resource_id"] = id;
      ss << "cvd-" << iface_ty_name << "-" << user_opt.value().substr(0, 4)
         << std::setfill('0') << std::setw(2) << (id % kMaxIfaceNameId);
      addedIface = AddInterface(ss.str(), iface_type, id, uid);
      --attempts;
    } while (!addedIface && (attempts > 0));
  }

  if (addedIface) {
    resp["request_status"] = StatusToStr(RequestStatus::Success);
    resp["iface_name"] = ss.str();
    resp["error"] = "";
  }

  return resp;
}

Json::Value ResourceManager::JsonHandleDestroyInterfaceRequest(
    const Json::Value& request) {
  Json::Value resp;
  resp["request_type"] = "destroy_interface";
  resp["request_status"] = StatusToStr(RequestStatus::Failure);
  if (!request.isMember("iface_name") || !request["iface_name"].isString()) {
    auto err_msg = "Input event doesn't have a valid 'iface_name' field";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  auto iface_name = request["iface_name"].asString();

  bool isManagedIface = active_interfaces_.erase(iface_name) > 0;

  if (!isManagedIface) {
    auto msg = "Interface not managed: " + iface_name;
    LOG(WARNING) << msg;
    resp["error"] = msg;
    return resp;
  }

  if (!request.isMember("session_id") || !request["session_id"].isUInt()) {
    auto err_msg = "Input event doesn't have a valid 'session_id' field";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  auto session_id = request["session_id"].asUInt();

  auto resource_id = request["resource_id"].asUInt();

  LOG(INFO) << "Received DestroyInterface Request for " << iface_name
            << " in session: " << session_id
            << ", resource_id: " << resource_id;

  auto sess_opt = FindSession(session_id);
  if (!sess_opt) {
    auto msg = "Interface " + iface_name +
               " was not managed in session: " + std::to_string(session_id) +
               " with resource_id: " + std::to_string(resource_id);
    LOG(WARNING) << msg;
    resp["error"] = msg;
    return resp;
  }

  auto s = sess_opt.value();

  // while we could wait to see if any acquisitions fail and delay releasing
  // resources until they are all finished, this operation is inherently
  // destructive, so should a release operation fail, there is no satisfactory
  // method for aborting the transaction. Instead, we try to release the
  // resource and then can signal to the rest of the transaction the failure
  // state, which can then just stop the transaction, and revert any newly
  // acquired resources, but any successful drop requests will persist
  auto did_drop_resource = s->ReleaseResource(resource_id);

  if (did_drop_resource) {
    resp["request_status"] = StatusToStr(RequestStatus::Success);
  } else {
    auto msg = "Interface " + iface_name +
               " was not managed in session: " + std::to_string(session_id) +
               " with resource_id: " + std::to_string(resource_id);
    LOG(WARNING) << msg;
    resp["error"] = msg;
  }

  return resp;
}

Json::Value ResourceManager::JsonHandleStopSessionRequest(
    const Json::Value& request, uid_t uid) {
  Json::Value resp;
  resp["request_type"] = ReqTyToStr(RequestType::StopSession);
  resp["request_status"] = StatusToStr(RequestStatus::Failure);
  if (!request.isMember("session_id") || !request["session_id"].isUInt()) {
    auto err_msg = "Input event doesn't have a valid 'session_id' field";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  auto session_id = request["session_id"].asUInt();
  LOG(INFO) << "Received StopSession Request for Session ID: " << session_id;

  auto it = managed_sessions_.find(session_id);
  if (it == managed_sessions_.end()) {
    auto msg = "Session not managed: " + std::to_string(session_id);
    LOG(WARNING) << msg;
    resp["error"] = msg;
    return resp;
  }

  if (it->second->GetUID() != uid) {
    auto msg = "Effective user ID does not match session owner. socket uid: " +
               std::to_string(uid);
    LOG(WARNING) << msg;
    resp["error"] = msg;
    return resp;
  }

  // while we could wait to see if any acquisitions fail and delay releasing
  // resources until they are all finished, this operation is inherently
  // destructive, so should a release operation fail, there is no satisfactory
  // method for aborting the transaction. Instead, we try to release the
  // resource and then can signal to the rest of the transaction the failure
  // state
  auto success = it->second->ReleaseAllResources();

  // release the names from the global list for reuse in future requests
  for (auto& iface : it->second->GetActiveInterfaces()) {
    active_interfaces_.erase(iface);
  }

  if (success) {
    managed_sessions_.erase(it);
    resp["request_status"] = StatusToStr(RequestStatus::Success);
  } else {
    resp["error"] =
        "unknown, allocd experienced an error ending the session id: " +
        std::to_string(session_id);
  }

  return resp;
}

std::optional<std::shared_ptr<Session>> ResourceManager::FindSession(
    uint32_t id) {
  auto it = managed_sessions_.find(id);
  if (it == managed_sessions_.end()) {
    return std::nullopt;
  } else {
    return it->second;
  }
}

}  // namespace cuttlefish

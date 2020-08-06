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
#include <sstream>

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
  for (auto iface : active_interfaces_) {
    // success &= DestroyTap(iface);
  }
  active_interfaces_.clear();
  for (auto& res : managed_resources_) {
    success &= res.second->ReleaseResource();
  }

  Json::Value resp;
  resp["request_type"] = "shutdown";
  resp["request_status"] = success ? "success" : "failure";
  SendJsonMsg(shutdown_socket_, resp);
  LOG(INFO) << "Daemon Shutdown complete";
  unlink(location.c_str());
}

void ResourceManager::SetSocketLocation(const std::string& sock_name) {
  location = sock_name;
}

uint32_t ResourceManager::AllocateID() {
  return global_id_.fetch_add(1, std::memory_order_relaxed);
}

bool ResourceManager::AddInterface(std::string iface, IfaceType ty, uint32_t id,
                                   uid_t uid) {
  bool didInsert = active_interfaces_.insert(iface).second;
  bool allocatedIface = false;

  std::shared_ptr<StaticResource> res = nullptr;

  if (didInsert) {
    const char* idp = iface.c_str() + (iface.size() - 3);
    int small_id = atoi(idp);
    // allocatedIface = create_tap(iface);
    switch (ty) {
      case IfaceType::mtap: {
        res =
            std::make_shared<MobileIface>(iface, uid, small_id, id, kMobileIp);
        allocatedIface = res->AcquireResource();
        pending_add_.insert({id, res});
        // allocatedIface = CreateMobileIface(iface, small_id, kMobileIp);
        break;
      }
      case IfaceType::wtap: {
        res = std::make_shared<WirelessIface>(iface, uid, small_id, id,
                                              kMobileIp);
        allocatedIface = res->AcquireResource();
        pending_add_.insert({id, res});
        // allocatedIface = CreateWirelessIface(iface, use_ipv4_, use_ipv6_);
        break;
      }
      case IfaceType::wbr: {
        allocatedIface = CreateBridge(iface);
        break;
      }
      case IfaceType::Invalid:
        break;
    }
  } else {
    LOG(WARNING) << "Interface already in use: " << iface;
  }

  if (didInsert && !allocatedIface) {
    LOG(WARNING) << "Failed to allocate interface: " << iface;
    active_interfaces_.erase(iface);
    auto it = pending_add_.find(id);
    it->second->ReleaseResource();
    pending_add_.erase(it);
  }

  LOG(INFO) << "Finish CreateInterface Request";

  return allocatedIface;
}

bool ResourceManager::RemoveInterface(std::string iface, IfaceType ty) {
  bool isManagedIface = active_interfaces_.erase(iface) > 0;
  bool removedIface = false;
  if (isManagedIface) {
    switch (ty) {
      case IfaceType::mtap: {
        const char* idp = iface.c_str() + (iface.size() - 3);
        int id = atoi(idp);
        removedIface = DestroyMobileIface(iface, id, kMobileIp);
        break;
      }
      case IfaceType::wtap: {
        removedIface =
            DestroyWirelessIface(iface, use_ipv4_bridge_, use_ipv6_bridge_);
        break;
      }
      case IfaceType::wbr: {
        removedIface = DestroyBridge(iface);
        break;
      }
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

    Json::ArrayIndex size = req_list.size();

    Json::Value config_response;
    Json::Value response_list;

    // sentinel value, so we can populate the list of responses correctly
    // without trying to satisfy requests that will be aborted
    bool transaction_failed = false;

    for (Json::ArrayIndex i = 0; i < size; ++i) {
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
          response = JsonHandleIdRequest(client_socket);
          break;
        }
        case RequestType::Shutdown: {
          if (i != 0 || size != 1) {
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
            // TODO (paulkirth): figure out how to perform shutdown w/ larger
            // json
            return;
          }
        }
        case RequestType::CreateInterface: {
          response = JsonHandleCreateInterfaceRequest(client_socket, req);
          break;
        }
        case RequestType::DestroyInterface: {
          response = JsonHandleDestroyInterfaceRequest(client_socket, req);
          break;
        }
        case RequestType::Invalid: {
          LOG(WARNING) << "Invalid Request Type: " << req["request_type"];
          break;
        }
      }

      response_list.append(response);
      if (!(response["request_status"].asString() == "success")) {
        LOG(INFO) << "Request failed:" << req;
        transaction_failed = true;
        continue;
      }
    }

    config_response["response_list"] = response_list;
    config_response["config_status"] =
        transaction_failed ? "failure" : "success";

    if (!transaction_failed) {
      // commit the resources
      managed_resources_.insert(pending_add_.begin(), pending_add_.end());
      pending_add_.clear();
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

bool ResourceManager::CheckCredentials(SharedFD client_socket, uid_t uid) {
  bool success = true;

  // do nothing code to avoid unused parameters. remove if enabling checks
  if (!uid || !client_socket->IsOpen()) {
    return false;
  }

  struct ucred ucred {};
  socklen_t len = sizeof(struct ucred);

  // TODO (paulkirth): replace magic number for error code
  if (-1 == client_socket->GetSockOpt(SOL_SOCKET, SO_PEERCRED, &ucred, &len)) {
    LOG(WARNING) << "Failed to get Socket Credentials";
    // TODO (paulkirth): check erno and failure conditions
    return false;
  }

  if (uid != ucred.uid) {
    LOG(WARNING) << "Message UID: " << uid
                 << " does not match socket's EUID: " << ucred.uid;
    success = false;
  }

  return success;
}

Json::Value ResourceManager::JsonHandleIdRequest(SharedFD client_socket) {
  Json::Value resp;
  resp["request_type"] = "allocate_id";
  resp["request_status"] = "success";
  resp["id"] = AllocateID();
  // TODO(paulkirth): remove
  if (client_socket->IsOpen()) {
  }
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
  resp["request_status"] = "failure";
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
    auto iface_type = StrToIfaceTy(iface_ty_name);
    auto attempts = kMaxIfaceNameId;
    do {
      auto id = AllocateID();
      resp["global_id"] = id;
      ss << "cvd-" << iface_ty_name << "-" << user_opt.value().substr(0, 4)
         << std::setfill('0') << std::setw(2) << (id % kMaxIfaceNameId);
      addedIface = AddInterface(ss.str(), iface_type, id, uid);
      --attempts;
    } while (!addedIface && (attempts > 0));
  }

  if (addedIface) {
    resp["request_status"] = "success";
    resp["iface_name"] = ss.str();
    resp["error"] = "";
  }

  // SendJsonMsg(client_socket, resp);
  return resp;
}

Json::Value ResourceManager::JsonHandleDestroyInterfaceRequest(
    SharedFD client_socket, const Json::Value& request) {
  Json::Value resp;
  resp["request_type"] = "destroy_interface";
  resp["request_status"] = "failure";
  if (!request.isMember("iface_name") || !request["iface_name"].isString()) {
    auto err_msg = "Input event doesn't have a valid 'iface_name' field";
    LOG(WARNING) << err_msg;
    resp["error"] = err_msg;
    return resp;
  }

  auto iface_name = request["iface_name"].asString();
  LOG(INFO) << "Received DestroyInterface Request for " << iface_name;

  auto global_id = request["global_id"].asUInt();

  auto it = managed_resources_.find(global_id);
  if (it == managed_resources_.end()) {
    auto msg = "Interface not managed: " + iface_name;
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
  auto success = it->second->ReleaseResource();

  if (success) {
    managed_resources_.erase(it);
    resp["request_status"] = "success";
  } else {
    resp["error"] = "unknown, could not release resource";
  }

  // auto iface_ty_name = request["iface_type"].asString();
  // auto iface_type = StrToIfaceTy(iface_ty_name);
  // auto success = RemoveInterface(iface_name, iface_type);

  if (client_socket->IsOpen()) {
  }
  return resp;
}

std::optional<std::shared_ptr<StaticResource>> ResourceManager::FindResource(
    uint32_t id) {
  auto it = managed_resources_.find(id);

  if (it == managed_resources_.end()) {
    return std::nullopt;
  } else {
    return it->second;
  }
}
}  // namespace cuttlefish

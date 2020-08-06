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
#include "json/writer.h"

namespace cuttlefish {

ResourceManager::~ResourceManager() {
  bool success = true;
  for (auto iface : active_interfaces_) {
    success &= DestroyTap(iface);
  }
  active_interfaces_.clear();

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

bool ResourceManager::AddInterface(std::string iface, IfaceType ty) {
  bool didInsert = active_interfaces_.insert(iface).second;

  bool allocatedIface = false;
  if (didInsert) {
    // allocatedIface = create_tap(iface);
    switch (ty) {
      case IfaceType::mtap: {
        const char* idp = iface.c_str() + (iface.size() - 3);
        int id = atoi(idp);
        allocatedIface = CreateMobileIface(iface, id, kMobileIp);
        break;
      }
      case IfaceType::wtap: {
        allocatedIface = CreateWirelessIface(iface, use_ipv4_, use_ipv6_);
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
  }

  return allocatedIface;
}

bool ResourceManager::RemoveInterface(std::string iface, IfaceType ty) {
  bool isManagedIface = active_interfaces_.erase(iface) > 0;
  bool removedIface = false;
  if (isManagedIface) {
    // success |= destroy_tap(iface);
    switch (ty) {
      case IfaceType::mtap: {
        const char* idp = iface.c_str() + (iface.size() - 3);
        int id = atoi(idp);
        removedIface = DestroyMobileIface(iface, id, kMobileIp);
        break;
      }
      case IfaceType::wtap: {
        removedIface = DestroyWirelessIface(iface, use_ipv4_, use_ipv6_);
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

bool ResourceManager::ValidateRequest(Json::Value& request) {
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

    if (!ValidateRequest(req)) {
      continue;
    }

    auto req_ty = StrToReqTy(req["request_type"].asString());

    switch (req_ty) {
      case RequestType::ID: {
        JsonHandleIdRequest(client_socket);
        break;
      }
      case RequestType::Shutdown: {
        JsonHandleShutdownRequest(client_socket);
        return;
      }
      case RequestType::CreateInterface: {
        JsonHandleCreateInterfaceRequest(client_socket, req);
        break;
      }
      case RequestType::DestroyInterface: {
        JsonHandleDestroyInterfaceRequest(client_socket, req);
        break;
      }
      case RequestType::Invalid: {
        LOG(WARNING) << "Invalid Request Type: " << req["request_type"];
        break;
      }
    }
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

void ResourceManager::JsonHandleIdRequest(SharedFD client_socket) {
  Json::Value resp;
  resp["request_type"] = "allocate_id";
  resp["request_status"] = "success";
  resp["id"] = AllocateID();
  SendJsonMsg(client_socket, resp);
}

void ResourceManager::JsonHandleShutdownRequest(SharedFD client_socket) {
  LOG(INFO) << "Received Shutdown Request";
  shutdown_socket_ = client_socket;
}

void ResourceManager::JsonHandleCreateInterfaceRequest(SharedFD client_socket,
                                                       Json::Value& request) {
  LOG(INFO) << "Received CreateInterface Request";

  if (!request.isMember("uid") || !request["uid"].isUInt()) {
    LOG(WARNING) << "Input event doesn't have a valid 'uid' field";
  }
  if (!request.isMember("iface_type") || !request["iface_type"].isString()) {
    LOG(WARNING) << "Input event doesn't have a valid 'iface_type' field";
  }

  auto uid = request["uid"].asUInt();

  const char* request_type = "request_type";
  Json::Value resp;
  resp[request_type] = "create_interface";
  resp["iface_name"] = "";
  resp["request_status"] = "failure";

  if (!CheckCredentials(client_socket, uid)) {
    LOG(WARNING) << "Credential check failed";
    resp["request_status"] = "failure";
    SendJsonMsg(client_socket, resp);
    return;
  }

  auto user_opt = GetUserName(uid);

  bool addedIface = false;
  std::stringstream ss;
  if (!user_opt) {
    LOG(WARNING) << "UserName could not be matched to UID, closing request";
  } else {
    auto iface_ty_name = request["iface_type"].asString();
    auto iface_type = StrToIfaceTy(iface_ty_name);
    // TODO (paulkirth): ID part of interface can only be 0-99, so maybe track
    // in an array/bitset?
    ss << "cvd-" << iface_ty_name << "-" << user_opt.value().substr(0, 4)
       << std::setfill('0') << std::setw(2) << AllocateID() % 100;
    addedIface = AddInterface(ss.str(), iface_type);
  }

  if (addedIface) {
    resp["request_status"] = "success";
    resp["iface_name"] = ss.str();
  }

  SendJsonMsg(client_socket, resp);
}

void ResourceManager::JsonHandleDestroyInterfaceRequest(SharedFD client_socket,
                                                        Json::Value& request) {
  if (!request.isMember("iface_name") || !request["iface_name"].isString()) {
    LOG(WARNING) << "Input event doesn't have a valid 'iface_name' field";
  }

  LOG(INFO) << "Received DestroyInterface Request for "
            << request["iface_name"].asString();
  Json::Value resp;
  resp["request_type"] = "destroy_interface";
  auto iface_ty_name = request["iface_type"].asString();
  auto iface_type = StrToIfaceTy(iface_ty_name);
  auto success = RemoveInterface(iface_ty_name, iface_type);
  resp["request_status"] = (success ? "success" : "failure");
  SendJsonMsg(client_socket, resp);
}

}  // namespace cuttlefish

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

#pragma once

#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include "alloc_utils.h"
#include "common/libs/fs/shared_fd.h"
#include "request.h"
#include "resource.h"
#include "utils.h"

namespace cuttlefish {

class Session {
 public:
  explicit Session(uint32_t session_id, uid_t uid)
      : session_id_(session_id), uid_(uid) {}
  ~Session() { ReleaseAllResources(); }

  uint32_t GetSessionID() { return session_id_; }
  uid_t GetUID() { return uid_; }

  const std::set<std::string>& GetActiveInterfaces() {
    return active_interfaces_;
  }

  void Insert(
      const std::map<uint32_t, std::shared_ptr<StaticResource>>& resources) {
    managed_resources_.insert(resources.begin(), resources.end());
  }

  bool ReleaseAllResources() {
    bool success = true;
    for (auto& res : managed_resources_) {
      success &= res.second->ReleaseResource();
    }
    managed_resources_.clear();

    return success;
  }

  bool ReleaseResource(uint32_t resource_id) {
    auto it = managed_resources_.find(resource_id);
    if (it == managed_resources_.end()) {
      return false;
    }

    auto success = it->second->ReleaseResource();
    if (success) {
      managed_resources_.erase(it);
    }

    return success;
  }

 private:
  uint32_t session_id_{};
  uid_t uid_{};
  std::set<std::string> active_interfaces_;
  std::map<uint32_t, std::shared_ptr<StaticResource>> managed_resources_;
};

/* Manages static resources while the daemon is running.
 * When resources, such as network interfaces are requested the ResourceManager
 * allocates the resources and takes ownership of them. It will keep maintain
 * the resource, until requested to release it(i.e. destroy it and/or tear down
 * related config). When the daemon is stopped, it will walk its list of owned
 * resources, and deallocate them from the system.
 *
 * Clients can request new resources by connecting to a socket, and sending a
 * JSON request, detailing the type of resource required.
 */
struct ResourceManager {
 public:
  ResourceManager() = default;

  ~ResourceManager();

  void SetSocketLocation(const std::string& sock_name);

  void SetUseEbtablesLegacy(bool use_legacy);

  void JsonServer();

 private:
  uint32_t AllocateResourceID();
  uint32_t AllocateSessionID();

  bool AddInterface(const std::string& iface, IfaceType ty, uint32_t id,
                    uid_t uid);

  bool RemoveInterface(const std::string& iface, IfaceType ty);

  bool ValidateRequest(const Json::Value& request);

  bool ValidateRequestList(const Json::Value& config);

  bool ValidateConfigRequest(const Json::Value& config);

  Json::Value JsonHandleIdRequest();

  Json::Value JsonHandleShutdownRequest(SharedFD client_socket);

  Json::Value JsonHandleCreateInterfaceRequest(SharedFD client_socket,
                                               const Json::Value& request);

  Json::Value JsonHandleDestroyInterfaceRequest(const Json::Value& request);

  Json::Value JsonHandleStopSessionRequest(const Json::Value& request,
                                           uid_t uid);

  bool CheckCredentials(SharedFD client_socket, uid_t uid);

  void SetUseIpv4Bridge(bool ipv4) { use_ipv4_bridge_ = ipv4; }

  void SetUseIpv6Bridge(bool ipv6) { use_ipv6_bridge_ = ipv6; }

  std::optional<std::shared_ptr<Session>> FindSession(uint32_t id);

 private:
  std::atomic_uint32_t global_resource_id_ = 0;
  std::atomic_uint32_t session_id_ = 0;
  std::set<std::string> active_interfaces_;
  std::map<uint32_t, std::shared_ptr<Session>> managed_sessions_;
  std::map<uint32_t, std::shared_ptr<StaticResource>> pending_add_;
  std::string location_ = kDefaultLocation;
  bool use_ipv4_bridge_ = true;
  bool use_ipv6_bridge_ = true;
  bool use_ebtables_legacy_ = false;
  cuttlefish::SharedFD shutdown_socket_;
};

}  // namespace cuttlefish

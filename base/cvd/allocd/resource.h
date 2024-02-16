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

#include <cstdint>
#include <string>

namespace cuttlefish {

enum class ResourceType {
  Invalid = 0,
  MobileIface,
  EthernetIface,
  EthernetBridge,
};

class StaticResource {
 public:
  StaticResource() = default;
  StaticResource(const std::string& name, uid_t uid, ResourceType ty,
                 uint32_t global_id)
      : name_(name), uid_(uid), global_id_(global_id), ty_(ty){};
  virtual ~StaticResource() = default;
  virtual bool ReleaseResource() = 0;
  virtual bool AcquireResource() = 0;

  std::string GetName() { return name_; }
  uid_t GetUid() { return uid_; }
  ResourceType GetResourceType() { return ty_; }
  uint32_t GetGlobalID() { return global_id_; }

 private:
  std::string name_{};
  uid_t uid_{};
  uint32_t global_id_{};
  ResourceType ty_ = ResourceType::Invalid;
};

class MobileIface : public StaticResource {
 public:
  MobileIface() = default;
  ~MobileIface() = default;
  MobileIface(const std::string& name, uid_t uid, uint16_t iface_id,
              uint32_t global_id, std::string ipaddr)
      : StaticResource(name, uid, ResourceType::MobileIface, global_id),
        iface_id_(iface_id),
        ipaddr_(ipaddr) {}

  bool ReleaseResource() override;
  bool AcquireResource() override;

  uint16_t GetIfaceId() { return iface_id_; }
  std::string GetIpAddr() { return ipaddr_; }

  static constexpr char kNetmask[] = "/30";

 private:
  uint16_t iface_id_;
  std::string ipaddr_;
};

class EthernetIface : public StaticResource {
 public:
  EthernetIface() = default;
  ~EthernetIface() = default;

  EthernetIface(const std::string& name, uid_t uid, uint16_t iface_id,
                uint32_t global_id, std::string bridge_name,
                std::string ipaddr)
      : StaticResource(name, uid, ResourceType::MobileIface, global_id),
        iface_id_(iface_id),
        bridge_name_(bridge_name),
        ipaddr_(ipaddr) {}

  bool ReleaseResource() override;
  bool AcquireResource() override;

  uint16_t GetIfaceId() { return iface_id_; }

  std::string GetBridgeName() { return bridge_name_; }
  std::string GetIpAddr() { return ipaddr_; }

  void SetHasIpv4(bool ipv4) { has_ipv4_ = ipv4; }
  void SetHasIpv6(bool ipv6) { has_ipv6_ = ipv6; }
  void SetUseEbtablesLegacy(bool use_legacy) {
    use_ebtables_legacy_ = use_legacy;
  }

  bool GetHasIpv4() { return has_ipv4_; }
  bool GetHasIpv6() { return has_ipv6_; }
  bool GetUseEbtablesLegacy() { return use_ebtables_legacy_; }

 private:
  static constexpr char kNetmask[] = "/24";
  uint16_t iface_id_;
  std::string bridge_name_;
  std::string ipaddr_;
  bool has_ipv4_ = true;
  bool has_ipv6_ = true;
  bool use_ebtables_legacy_ = false;
};

}  // namespace cuttlefish

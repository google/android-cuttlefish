//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/run_cvd/launch/launch.h"

#include "common/libs/utils/files.h"
#include "common/libs/utils/network.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_builder.h"
#include "host/libs/vm_manager/crosvm_manager.h"

namespace cuttlefish {
namespace {

class OpenWrt : public CommandSource {
 public:
  INJECT(OpenWrt(const CuttlefishConfig& config,
                 const CuttlefishConfig::InstanceSpecific& instance,
                 LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    constexpr auto crosvm_for_ap_socket = "ap_control.sock";

    CrosvmBuilder ap_cmd;
    ap_cmd.Cmd().SetExecutableAndName(config_.crosvm_binary());
    ap_cmd.Cmd().AddParameter("run");
    ap_cmd.AddControlSocket(
        instance_.PerInstanceInternalPath(crosvm_for_ap_socket),
        config_.crosvm_binary());

    if (!config_.vhost_user_mac80211_hwsim().empty()) {
      ap_cmd.Cmd().AddParameter("--vhost-user-mac80211-hwsim=",
                                config_.vhost_user_mac80211_hwsim());
    }
    SharedFD wifi_tap = ap_cmd.AddTap(instance_.wifi_tap_name());
    // Only run the leases workaround if we are not using the new network
    // bridge architecture - in that case, we have a wider DHCP address
    // space and stale leases should be much less of an issue
    if (!FileExists("/var/run/cuttlefish-dnsmasq-cvd-wbr.leases") &&
        wifi_tap->IsOpen()) {
      // TODO(schuffelen): QEMU also needs this and this is not the best place
      // for this code. Find a better place to put it.
      auto lease_file =
          ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-") +
          ".leases";
      std::uint8_t dhcp_server_ip[] = {
          192, 168, 96, (std::uint8_t)(ForCurrentInstance(1) * 4 - 3)};
      if (!ReleaseDhcpLeases(lease_file, wifi_tap, dhcp_server_ip)) {
        LOG(ERROR)
            << "Failed to release wifi DHCP leases. Connecting to the wifi "
            << "network may not work.";
      }
    }
    if (instance_.enable_sandbox()) {
      ap_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                                config_.seccomp_policy_dir());
    } else {
      ap_cmd.Cmd().AddParameter("--disable-sandbox");
    }
    ap_cmd.AddReadWriteDisk(instance_.PerInstancePath("ap_overlay.img"));
    ap_cmd.AddReadWriteDisk(instance_.persistent_ap_composite_disk_path());

    auto boot_logs_path =
        instance_.PerInstanceLogPath("crosvm_openwrt_boot.log");
    auto logs_path = instance_.PerInstanceLogPath("crosvm_openwrt.log");
    ap_cmd.AddSerialConsoleReadOnly(boot_logs_path);
    ap_cmd.AddHvcReadOnly(logs_path);

    ap_cmd.Cmd().AddParameter("--bios=", instance_.bootloader());

    std::vector<Command> commands;
    commands.emplace_back(log_tee_.CreateLogTee(ap_cmd.Cmd(), "openwrt"));
    commands.emplace_back(std::move(ap_cmd.Cmd()));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "OpenWrt"; }
  bool Enabled() const override {
    return instance_.start_ap() &&
           config_.vm_manager() == vm_manager::CrosvmManager::name();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
};

}  // namespace

fruit::Component<
    fruit::Required<const CuttlefishConfig,
                    const CuttlefishConfig::InstanceSpecific, LogTeeCreator>>
OpenWrtComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, OpenWrt>()
      .addMultibinding<SetupFeature, OpenWrt>();
}

}  // namespace cuttlefish

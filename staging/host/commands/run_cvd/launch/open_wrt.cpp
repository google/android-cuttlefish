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
#include "host/commands/run_cvd/launch/wmediumd_server.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/result.h"
#include "host/libs/command_util/snapshot_utils.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/config/openwrt_args.h"
#include "host/libs/vm_manager/crosvm_builder.h"
#include "host/libs/vm_manager/crosvm_manager.h"

namespace cuttlefish {
namespace {

using APBootFlow = CuttlefishConfig::InstanceSpecific::APBootFlow;

// TODO(b/288987294) Remove dependency to InstanceSpecific config when moving
// to run_env is completed.
class OpenWrt : public CommandSource {
 public:
  INJECT(OpenWrt(const CuttlefishConfig& config,
                 const CuttlefishConfig::EnvironmentSpecific& environment,
                 const CuttlefishConfig::InstanceSpecific& instance,
                 LogTeeCreator& log_tee, WmediumdServer& wmediumd_server))
      : config_(config),
        environment_(environment),
        instance_(instance),
        log_tee_(log_tee),
        wmediumd_server_(wmediumd_server) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    constexpr auto crosvm_for_ap_socket = "ap_control.sock";

    CrosvmBuilder ap_cmd;

    ap_cmd.Cmd().AddPrerequisite([this]() -> Result<void> {
      return wmediumd_server_.WaitForAvailability();
    });

    std::string first_time_argument;
    if (IsRestoring(config_)) {
      const std::string snapshot_dir_path = config_.snapshot_path();
      auto meta_info_json = CF_EXPECT(LoadMetaJson(snapshot_dir_path));
      const std::vector<std::string> selectors{kGuestSnapshotField,
                                               instance_.id()};
      const auto guest_snapshot_dir_suffix =
          CF_EXPECT(GetValue<std::string>(meta_info_json, selectors));
      // guest_snapshot_dir_suffix is a relative to
      // the snapshot_path
      const auto restore_path = snapshot_dir_path + "/" +
                                guest_snapshot_dir_suffix + "/" +
                                kGuestSnapshotBase + "_openwrt";
      first_time_argument = "--restore=" + restore_path;
    }

    /* TODO(b/305102099): Due to hostapd issue of OpenWRT 22.03.X versions,
     * OpenWRT instance should be rebooted.
     */
    LOG(DEBUG) << "Restart OpenWRT due to hostapd issue";
    ap_cmd.ApplyProcessRestarter(instance_.crosvm_binary(), first_time_argument,
                                 kOpenwrtVmResetExitCode);
    ap_cmd.Cmd().AddParameter("run");
    ap_cmd.AddControlSocket(
        instance_.PerInstanceInternalUdsPath(crosvm_for_ap_socket),
        instance_.crosvm_binary());

    ap_cmd.Cmd().AddParameter("--no-usb");
    ap_cmd.Cmd().AddParameter("--core-scheduling=false");

    if (!environment_.vhost_user_mac80211_hwsim().empty()) {
      ap_cmd.Cmd().AddParameter("--vhost-user=mac80211-hwsim,socket=",
                                environment_.vhost_user_mac80211_hwsim());
    }
    if (environment_.enable_wifi() && instance_.enable_tap_devices()) {
      ap_cmd.AddTap(instance_.wifi_tap_name());
    }

    /* TODO(kwstephenkim): delete this code when Minidroid completely disables
     * the AP VM itself
     */
    if (!instance_.crosvm_use_balloon()) {
      ap_cmd.Cmd().AddParameter("--no-balloon");
    }

    /* TODO(kwstephenkim): delete this code when Minidroid completely disables
     * the AP VM itself
     */
    if (!instance_.crosvm_use_rng()) {
      ap_cmd.Cmd().AddParameter("--no-rng");
    }

    if (instance_.enable_sandbox()) {
      ap_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                                instance_.seccomp_policy_dir());
    } else {
      ap_cmd.Cmd().AddParameter("--disable-sandbox");
    }
    ap_cmd.AddReadWriteDisk(instance_.PerInstancePath("ap_overlay.img"));

    auto boot_logs_path =
        instance_.PerInstanceLogPath("crosvm_openwrt_boot.log");
    auto logs_path = instance_.PerInstanceLogPath("crosvm_openwrt.log");
    ap_cmd.AddSerialConsoleReadOnly(boot_logs_path);
    ap_cmd.AddHvcReadOnly(logs_path);

    auto openwrt_args = OpenwrtArgsFromConfig(instance_);
    switch (instance_.ap_boot_flow()) {
      case APBootFlow::Grub:
        if (config_.vm_manager() == VmmMode::kQemu) {
          ap_cmd.AddReadWriteDisk(
              instance_.persistent_ap_composite_overlay_path());
        } else {
          ap_cmd.AddReadWriteDisk(
              instance_.persistent_ap_composite_disk_path());
        }
        ap_cmd.Cmd().AddParameter("--bios=", instance_.bootloader());
        break;
      case APBootFlow::LegacyDirect:
        ap_cmd.Cmd().AddParameter("--params=\"root=/dev/vda1\"");
        for (auto& openwrt_arg : openwrt_args) {
          ap_cmd.Cmd().AddParameter("--params=" + openwrt_arg.first + "=" +
                                    openwrt_arg.second);
        }
        ap_cmd.Cmd().AddParameter(config_.ap_kernel_image());
        break;
      default:
        // must not be happened
        break;
    }

    std::vector<MonitorCommand> commands;
    commands.emplace_back(
        CF_EXPECT(log_tee_.CreateFullLogTee(ap_cmd.Cmd(), "openwrt")));
    commands.emplace_back(std::move(ap_cmd.Cmd()));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "OpenWrt"; }
  bool Enabled() const override {
    return instance_.ap_boot_flow() != APBootFlow::None &&
           config_.vm_manager() == VmmMode::kCrosvm;
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override { return {}; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::EnvironmentSpecific& environment_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
  WmediumdServer& wmediumd_server_;

  static constexpr int kOpenwrtVmResetExitCode = 32;
};

}  // namespace

fruit::Component<fruit::Required<
    const CuttlefishConfig, const CuttlefishConfig::EnvironmentSpecific,
    const CuttlefishConfig::InstanceSpecific, LogTeeCreator, WmediumdServer>>
OpenWrtComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, OpenWrt>()
      .addMultibinding<SetupFeature, OpenWrt>();
}

}  // namespace cuttlefish

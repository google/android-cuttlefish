//
// Copyright (C) 2022 The Android Open Source Project
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

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

// NetsimServer launches netsim server with fifos for radio HALs.
//
// netsimd -s '{devices:[
//  {"name":"0.0.0.0:5000", "chips":[
//    {"kind":"BLUETOOTH", "fdIn":10, "fdOut":11}]},
//  {"name":"0.0.0.0:5010", "chips":[
//    {"kind":"BLUETOOTH", "fdIn":14, "fdOut":15}]}]}

// Chip and Device classes pass SharedFD fifos between ResultSetup and Commands
// and format the netsim json command line.

class Chip {
 public:
  SharedFD fd_in;
  SharedFD fd_out;

  Chip(std::string kind) : kind_(kind) {}

  // Append the chip information as Json to the command.
  void Append(Command& c) const {
    c.AppendToLastParameter(R"({"kind":")", kind_, R"(","fdIn":)", fd_in,
                            R"(,"fdOut":)", fd_out, "}");
  }

 private:
  std::string kind_;
};

class Device {
 public:
  Device(std::string name) : name_(name) {}

  void Append(Command& c) const {
    c.AppendToLastParameter(R"({"name":")", name_, R"(","chips":[)");
    for (int i = 0; i < chips.size(); ++i) {
      chips[i].Append(c);
      if (chips.size() - i > 1) {
        c.AppendToLastParameter(",");
      }
    }
    c.AppendToLastParameter("]}");
  }

  std::vector<Chip> chips;

 private:
  std::string name_;
};

class NetsimServer : public CommandSource {
 public:
  INJECT(NetsimServer(const CuttlefishConfig& config,
                      const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    Command netsimd(NetsimdBinary());
    netsimd.AddParameter("-s");
    AddDevicesParameter(netsimd);
    // Release SharedFDs, they've been duped by Command
    devices_.clear();
    // Port configuration.
    netsimd.AddParameter("--hci_port=", config_.rootcanal_hci_port());

    // When no connector is requested, add the instance number
    if (config_.netsim_connector_instance_num() ==
        config_.netsim_instance_num()) {
      // external instance numbers start at 1 not 0
      netsimd.AddParameter("--instance_num=",
                           config_.netsim_instance_num() + 1);
    } else {
      // If instance_num is not the target, then inform netsim to forward
      // packets to another netsim daemon that was launched from cuttlefish with
      // a different instance_num.
      netsimd.AddParameter("--connector_instance_num=",
                           config_.netsim_connector_instance_num() + 1);
    }

    // Add parameters from passthrough option --netsim-args.
    for (auto const& arg : config_.netsim_args()) {
      netsimd.AddParameter(arg);
    }

    // Add command for forwarding the HCI port to a vsock server.
    Command hci_vsock_proxy(SocketVsockProxyBinary());
    hci_vsock_proxy.AddParameter("--server_type=vsock");
    hci_vsock_proxy.AddParameter("--server_vsock_port=",
                                 config_.rootcanal_hci_port());
    hci_vsock_proxy.AddParameter("--server_vsock_id=",
                                 instance_.vsock_guest_cid());
    hci_vsock_proxy.AddParameter("--client_type=tcp");
    hci_vsock_proxy.AddParameter("--client_tcp_host=127.0.0.1");
    hci_vsock_proxy.AddParameter("--client_tcp_port=",
                                 config_.rootcanal_hci_port());

    // Add command for forwarding the test port to a vsock server.
    Command test_vsock_proxy(SocketVsockProxyBinary());
    test_vsock_proxy.AddParameter("--server_type=vsock");
    test_vsock_proxy.AddParameter("--server_vsock_port=",
                                  config_.rootcanal_test_port());
    test_vsock_proxy.AddParameter("--server_vsock_id=",
                                  instance_.vsock_guest_cid());
    test_vsock_proxy.AddParameter("--client_type=tcp");
    test_vsock_proxy.AddParameter("--client_tcp_host=127.0.0.1");
    test_vsock_proxy.AddParameter("--client_tcp_port=",
                                  config_.rootcanal_test_port());

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(netsimd));
    commands.emplace_back(std::move(hci_vsock_proxy));
    commands.emplace_back(std::move(test_vsock_proxy));
    return commands;
  }

  // Convert devices_ to json for netsimd -s <arg>. The devices_, created and
  // validated during ResultSetup, contains all the SharedFDs and meta-data.

  void AddDevicesParameter(Command& c) {
    c.AddParameter(R"({"devices":[)");
    for (int i = 0; i < devices_.size(); ++i) {
      devices_[i].Append(c);
      if (devices_.size() - i > 1) {
        c.AppendToLastParameter(",");
      }
    }
    c.AppendToLastParameter("]}");
  }

  // SetupFeature
  std::string Name() const override { return "Netsim"; }
  bool Enabled() const override { return instance_.start_netsim(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  Result<void> ResultSetup() {
    auto netsimd = HostBinaryPath("netsimd");
    CF_EXPECT(FileExists(netsimd),
              "Failed to find netsimd binary: " << netsimd);

    for (const auto& instance : config_.Instances()) {
      Device device(instance.adb_ip_and_port());
      // Add bluetooth chip if enabled
      if (config_.netsim_radio_enabled(
              CuttlefishConfig::NetsimRadio::Bluetooth)) {
        Chip chip("BLUETOOTH");
        chip.fd_in = CF_EXPECT(MakeFifo(instance, "bt_fifo_vm.in"));
        chip.fd_out = CF_EXPECT(MakeFifo(instance, "bt_fifo_vm.out"));
        device.chips.emplace_back(chip);
      }
      // Add uwb chip if enabled
      if (config_.netsim_radio_enabled(CuttlefishConfig::NetsimRadio::Uwb)) {
        Chip chip("UWB");
        chip.fd_in = CF_EXPECT(MakeFifo(instance, "uwb_fifo_vm.in"));
        chip.fd_out = CF_EXPECT(MakeFifo(instance, "uwb_fifo_vm.out"));
        device.chips.emplace_back(chip);
      }
      // Add other chips if enabled
      devices_.emplace_back(device);
    }
    return {};
  }

  Result<SharedFD> MakeFifo(const CuttlefishConfig::InstanceSpecific& instance,
                            const char* relative_path) {
    auto path = instance.PerInstanceInternalPath(relative_path);
    return CF_EXPECT(SharedFD::Fifo(path, 0660));
  }

 private:
  std::vector<Device> devices_;
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
NetsimServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, NetsimServer>()
      .addMultibinding<SetupFeature, NetsimServer>();
}

}  // namespace cuttlefish

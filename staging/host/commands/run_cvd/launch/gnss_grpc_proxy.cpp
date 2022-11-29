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
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class GnssGrpcProxyServer : public CommandSource {
 public:
  INJECT(
      GnssGrpcProxyServer(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    Command gnss_grpc_proxy_cmd(GnssGrpcProxyBinary());
    const unsigned gnss_grpc_proxy_server_port =
        instance_.gnss_grpc_proxy_server_port();
    gnss_grpc_proxy_cmd.AddParameter("--gnss_in_fd=", gnss_grpc_proxy_in_wr_);
    gnss_grpc_proxy_cmd.AddParameter("--gnss_out_fd=", gnss_grpc_proxy_out_rd_);
    gnss_grpc_proxy_cmd.AddParameter("--fixed_location_in_fd=",
                                     fixed_location_grpc_proxy_in_wr_);
    gnss_grpc_proxy_cmd.AddParameter("--fixed_location_out_fd=",
                                     fixed_location_grpc_proxy_out_rd_);
    gnss_grpc_proxy_cmd.AddParameter("--gnss_grpc_port=",
                                     gnss_grpc_proxy_server_port);
    if (!instance_.gnss_file_path().empty()) {
      // If path is provided, proxy will start as local mode.
      gnss_grpc_proxy_cmd.AddParameter("--gnss_file_path=",
                                       instance_.gnss_file_path());
    }
    if (!instance_.fixed_location_file_path().empty()) {
      gnss_grpc_proxy_cmd.AddParameter("--fixed_location_file_path=",
                                       instance_.fixed_location_file_path());
    }
    return single_element_emplace(std::move(gnss_grpc_proxy_cmd));
  }

  // SetupFeature
  std::string Name() const override { return "GnssGrpcProxyServer"; }
  bool Enabled() const override {
    return instance_.enable_gnss_grpc_proxy() &&
           FileExists(GnssGrpcProxyBinary());
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    std::vector<SharedFD> fifos;
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("gnsshvc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("gnsshvc_fifo_vm.out"),
        instance_.PerInstanceInternalPath("locationhvc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("locationhvc_fifo_vm.out"),
    };
    for (const auto& path : fifo_paths) {
      unlink(path.c_str());
      CF_EXPECT(mkfifo(path.c_str(), 0660) == 0, "Could not create " << path);
      auto fd = SharedFD::Open(path, O_RDWR);
      CF_EXPECT(fd->IsOpen(),
                "Could not open " << path << ": " << fd->StrError());
      fifos.push_back(fd);
    }

    gnss_grpc_proxy_in_wr_ = fifos[0];
    gnss_grpc_proxy_out_rd_ = fifos[1];
    fixed_location_grpc_proxy_in_wr_ = fifos[2];
    fixed_location_grpc_proxy_out_rd_ = fifos[3];
    return {};
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD gnss_grpc_proxy_in_wr_;
  SharedFD gnss_grpc_proxy_out_rd_;
  SharedFD fixed_location_grpc_proxy_in_wr_;
  SharedFD fixed_location_grpc_proxy_out_rd_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>>
GnssGrpcProxyServerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, GnssGrpcProxyServer>()
      .addMultibinding<SetupFeature, GnssGrpcProxyServer>();
}

}  // namespace cuttlefish

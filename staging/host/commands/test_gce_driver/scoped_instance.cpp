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

#include "host/commands/test_gce_driver/scoped_instance.h"

#include <netinet/ip.h>

#include <random>
#include <sstream>
#include <string>

#include <android-base/file.h>
#include <android-base/result.h>

#include "common/libs/fs/shared_buf.h"

using android::base::Error;
using android::base::Result;

namespace cuttlefish {

SshCommand& SshCommand::PrivKey(const std::string& privkey_path) & {
  privkey_path_ = privkey_path;
  return *this;
}
SshCommand SshCommand::PrivKey(const std::string& privkey_path) && {
  privkey_path_ = privkey_path;
  return *this;
}

SshCommand& SshCommand::WithoutKnownHosts() & {
  without_known_hosts_ = true;
  return *this;
}
SshCommand SshCommand::WithoutKnownHosts() && {
  without_known_hosts_ = true;
  return *this;
}

SshCommand& SshCommand::Username(const std::string& username) & {
  username_ = username;
  return *this;
}
SshCommand SshCommand::Username(const std::string& username) && {
  username_ = username;
  return *this;
}

SshCommand& SshCommand::Host(const std::string& host) & {
  host_ = host;
  return *this;
}
SshCommand SshCommand::Host(const std::string& host) && {
  host_ = host;
  return *this;
}

SshCommand& SshCommand::RemotePortForward(uint16_t remote, uint16_t local) & {
  remote_port_forwards_.push_back({remote, local});
  return *this;
}
SshCommand SshCommand::RemotePortForward(uint16_t remote, uint16_t local) && {
  remote_port_forwards_.push_back({remote, local});
  return *this;
}

SshCommand& SshCommand::RemoteParameter(const std::string& param) & {
  parameters_.push_back(param);
  return *this;
}
SshCommand SshCommand::RemoteParameter(const std::string& param) && {
  parameters_.push_back(param);
  return *this;
}

Command SshCommand::Build() const {
  Command remote_cmd{"/usr/bin/ssh"};
  if (privkey_path_) {
    remote_cmd.AddParameter("-i");
    remote_cmd.AddParameter(*privkey_path_);
  }
  if (without_known_hosts_) {
    remote_cmd.AddParameter("-o");
    remote_cmd.AddParameter("StrictHostKeyChecking=no");
    remote_cmd.AddParameter("-o");
    remote_cmd.AddParameter("UserKnownHostsFile=/dev/null");
  }
  for (const auto& fwd : remote_port_forwards_) {
    remote_cmd.AddParameter("-R");
    remote_cmd.AddParameter(fwd.remote_port, ":127.0.0.1:", fwd.local_port);
  }
  if (host_) {
    remote_cmd.AddParameter(username_ ? *username_ + "@" : "", *host_);
  }
  for (const auto& param : parameters_) {
    remote_cmd.AddParameter(param);
  }
  return remote_cmd;
}

Result<std::unique_ptr<ScopedGceInstance>> ScopedGceInstance::CreateDefault(
    GceApi& gce, const std::string& zone, const std::string& instance_name,
    bool internal) {
  auto ssh_key = KeyPair::CreateRsa(4096);
  if (!ssh_key.ok()) {
    return Error() << "Could not create ssh key pair: " << ssh_key.error();
  }

  auto ssh_pubkey = (*ssh_key)->OpenSshPublicKey();
  if (!ssh_pubkey.ok()) {
    return Error() << "Could get openssh format key: " << ssh_pubkey.error();
  }

  auto default_instance_info =
      GceInstanceInfo()
          .Name(instance_name)
          .Zone(zone)
          .MachineType("zones/us-west1-a/machineTypes/n1-standard-4")
          .AddMetadata("ssh-keys", "vsoc-01:" + *ssh_pubkey)
          .AddNetworkInterface(GceNetworkInterface::Default())
          .AddDisk(
              GceInstanceDisk::EphemeralBootDisk()
                  .SourceImage(
                      "projects/cloud-android-releases/global/images/family/"
                      "cuttlefish-google")
                  .SizeGb(30))
          .AddScope("https://www.googleapis.com/auth/androidbuild.internal")
          .AddScope("https://www.googleapis.com/auth/devstorage.read_only")
          .AddScope("https://www.googleapis.com/auth/logging.write");

  auto creation = gce.Insert(default_instance_info).Future().get();
  if (!creation.ok()) {
    return Error() << "Failed to create instance: " << creation.error();
  }

  auto privkey = CF_EXPECT((*ssh_key)->PemPrivateKey());
  std::unique_ptr<TemporaryFile> privkey_file(CF_EXPECT(new TemporaryFile()));
  auto fd_dup = SharedFD::Dup(privkey_file->fd);
  CF_EXPECT(fd_dup->IsOpen());
  CF_EXPECT(WriteAll(fd_dup, privkey) == privkey.size());
  fd_dup->Close();

  std::unique_ptr<ScopedGceInstance> instance(new ScopedGceInstance(
      gce, default_instance_info, std::move(privkey_file), internal));

  auto created_info = gce.Get(default_instance_info).get();
  if (!created_info.ok()) {
    return Error() << "Failed to get instance info: " << created_info.error();
  }
  instance->instance_ = *created_info;

  auto ssh_ready = instance->EnforceSshReady();
  if (!ssh_ready.ok()) {
    return Error() << "Failed to access SSH on instance: " << ssh_ready.error();
  }
  return instance;
}

Result<void> ScopedGceInstance::EnforceSshReady() {
  std::string out;
  std::string err;
  for (int i = 0; i < 100; i++) {
    auto ssh = Ssh();
    if (!ssh.ok()) {
      return Error() << "Failed to create ssh command: " << ssh.error();
    }

    ssh->RemoteParameter("ls");
    ssh->RemoteParameter("/");
    auto command = ssh->Build();

    out = "";
    err = "";
    int ret = RunWithManagedStdio(std::move(command), nullptr, &out, &err);
    if (ret == 0) {
      return {};
    }
  }

  return Error() << "Failed to ssh to the instance. stdout=\"" << out
                 << "\", stderr = \"" << err << "\"";
}

ScopedGceInstance::ScopedGceInstance(GceApi& gce,
                                     const GceInstanceInfo& instance,
                                     std::unique_ptr<TemporaryFile> privkey,
                                     bool use_internal_address)
    : gce_(gce),
      instance_(instance),
      privkey_(std::move(privkey)),
      use_internal_address_(use_internal_address) {}

ScopedGceInstance::~ScopedGceInstance() {
  auto delete_ins = gce_.Delete(instance_).Future().get();
  if (!delete_ins.ok()) {
    LOG(ERROR) << "Failed to delete instance: " << delete_ins.error();
  }
}

Result<SshCommand> ScopedGceInstance::Ssh() {
  const auto& network_interfaces = instance_.NetworkInterfaces();
  CF_EXPECT(!network_interfaces.empty());
  auto iface = network_interfaces[0];
  auto ip = use_internal_address_ ? iface.InternalIp() : iface.ExternalIp();
  CF_EXPECT(ip.has_value());
  return SshCommand()
      .PrivKey(privkey_->path)
      .WithoutKnownHosts()
      .Username("vsoc-01")
      .Host(*ip);
}

}  // namespace cuttlefish

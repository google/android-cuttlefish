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

#include <android-base/result.h>

#include "common/libs/fs/shared_buf.h"

using android::base::Error;
using android::base::Result;

namespace cuttlefish {

SshCommand& SshCommand::PrivKey(const std::string& privkey) & {
  privkey_ = privkey;
  return *this;
}
SshCommand SshCommand::PrivKey(const std::string& privkey) && {
  privkey_ = privkey;
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

static uint16_t RandomTcpPort() {
  std::random_device rand_dev;
  std::mt19937 generator(rand_dev());
  std::uniform_int_distribution<uint16_t> distr(1024, 65535);
  return distr(generator);
}

/*
 * Netcat is used here as part of a sequence of workarounds:
 *
 * `ssh` closes every file descriptor above stderr (2) on startup
 * ... so we pass the private key to `ssh` in a file descriptor 0, or /dev/stdin
 * ... so stdin is unavailable to pass data to the remote side
 * ... so we use `ssh`'s reverse port forward feature to send data over TCP
 * ... so we invoke netcat on the remote side to save this into a file.
 *
 * There is some room to generalize this in the future to support interactive
 * processes to recover the functionality lost with using stdin for other
 * purposes.
 */
Result<SharedFD> SshCommand::TcpServerStdin() & {
  if (parameters_.size() > 0 && parameters_.front() == "netcat") {
    return Error() << "TcpServerStdin was already called";
  }
  auto server = SharedFD::SocketLocalServer(0, SOCK_STREAM);
  if (!server->IsOpen()) {
    return Error() << "Could not allocate TCP server: " << server->StrError();
  }

  struct sockaddr_in address;
  struct sockaddr* untyped_addr = reinterpret_cast<struct sockaddr*>(&address);
  socklen_t length = sizeof(address);
  if (server->GetSockName(untyped_addr, &length) != 0) {
    return Error() << "GetSockName failed: " << server->StrError();
  }
  auto local_port = ntohs(address.sin_port);  // sin_port has network byte order

  auto remote_port = RandomTcpPort();
  // SSH always creates a shell on the remote side, so we don't need to
  // explicitly create it with `bash -c "..."`.
  RemotePortForward(remote_port, local_port);
  parameters_.insert(parameters_.begin(),
                     {"netcat", "127.0.0.1", std::to_string(remote_port), "|"});
  return server;
}

Command SshCommand::Build() const {
  Command remote_cmd{"/usr/bin/ssh"};
  remote_cmd.AddParameter("-n");
  if (privkey_) {
    // OpenSSH closes file descriptors higher than stderr
    // https://github.com/openssh/openssh-portable/blob/6b977f8080a32c5b3cbb9edb634b9d5789fb79be/ssh.c#L642
    remote_cmd.AddParameter("-i");
    remote_cmd.AddParameter("/proc/self/fd/0");
    remote_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn,
                             SharedFD::MemfdCreateWithData("", *privkey_));
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
    GceApi& gce, const std::string& instance_name) {
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

  std::unique_ptr<ScopedGceInstance> instance(
      new ScopedGceInstance(gce, default_instance_info, std::move(*ssh_key)));

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

    /**
     * Any command works here, so we might as well try a command to install
     * netcat. Netcat is used later to compensate for stdin being unavailable
     * on the SSH commands used here.
     */
    ssh->RemoteParameter("sudo");
    ssh->RemoteParameter("apt-get");
    ssh->RemoteParameter("install");
    ssh->RemoteParameter("-y");
    ssh->RemoteParameter("netcat");
    auto getNetcat = ssh->Build();

    out = "";
    err = "";
    int ret = RunWithManagedStdio(std::move(getNetcat), nullptr, &out, &err);
    if (ret == 0) {
      return {};
    }
  }

  return Error() << "Failed to ssh to the instance. stdout=\"" << out
                 << "\", stderr = \"" << err << "\"";
}

ScopedGceInstance::ScopedGceInstance(GceApi& gce,
                                     const GceInstanceInfo& instance,
                                     std::unique_ptr<KeyPair> keypair)
    : gce_(gce), instance_(instance), keypair_(std::move(keypair)) {}

ScopedGceInstance::~ScopedGceInstance() {
  auto delete_ins = gce_.Delete(instance_).Future().get();
  if (!delete_ins.ok()) {
    LOG(ERROR) << "Failed to delete instance: " << delete_ins.error();
  }
}

Result<SshCommand> ScopedGceInstance::Ssh() {
  auto ssh_privkey = keypair_->PemPrivateKey();
  if (!ssh_privkey.ok()) {
    return Error() << "Failed to get private key: " << ssh_privkey.error();
  }

  return SshCommand()
      .PrivKey(*ssh_privkey)
      .WithoutKnownHosts()
      .Username("vsoc-01")
      .Host(instance_.NetworkInterfaces()[0].ExternalIp().value());
}

Result<void> ScopedGceInstance::Reset() {
  auto internal_reset = gce_.Reset(instance_).Future().get();
  if (!internal_reset.ok()) {
    return Error() << "GCE reset failed: " << internal_reset.error();
  }
  return EnforceSshReady();
}

}  // namespace cuttlefish

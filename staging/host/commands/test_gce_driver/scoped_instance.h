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

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <android-base/file.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/test_gce_driver/gce_api.h"

namespace cuttlefish {

// TODO(schuffelen): Implement this with libssh2
class SshCommand {
 public:
  SshCommand() = default;

  SshCommand& PrivKey(const std::string& path) &;
  SshCommand PrivKey(const std::string& path) &&;

  SshCommand& WithoutKnownHosts() &;
  SshCommand WithoutKnownHosts() &&;

  SshCommand& Username(const std::string& username) &;
  SshCommand Username(const std::string& username) &&;

  SshCommand& Host(const std::string& host) &;
  SshCommand Host(const std::string& host) &&;

  SshCommand& RemotePortForward(uint16_t remote, uint16_t local) &;
  SshCommand RemotePortForward(uint16_t remote, uint16_t local) &&;

  SshCommand& RemoteParameter(const std::string& param) &;
  SshCommand RemoteParameter(const std::string& param) &&;

  Command Build() const;

 private:
  struct RemotePortForwardType {
    uint16_t remote_port;
    uint16_t local_port;
  };

  std::optional<std::string> privkey_path_;
  bool without_known_hosts_;
  std::optional<std::string> username_;
  std::optional<std::string> host_;
  std::vector<RemotePortForwardType> remote_port_forwards_;
  std::vector<std::string> parameters_;
};

class ScopedGceInstance {
 public:
  static Result<std::unique_ptr<ScopedGceInstance>> CreateDefault(
      GceApi& gce, const std::string& zone, const std::string& instance_name,
      bool internal_addresses);
  ~ScopedGceInstance();

  Result<SshCommand> Ssh();
  Result<void> Reset();

 private:
  ScopedGceInstance(GceApi& gce, const GceInstanceInfo& instance,
                    std::unique_ptr<TemporaryFile> privkey,
                    bool internal_addresses);

  Result<void> EnforceSshReady();

  GceApi& gce_;
  GceInstanceInfo instance_;
  std::unique_ptr<TemporaryFile> privkey_;
  bool use_internal_address_;
};

}  // namespace cuttlefish

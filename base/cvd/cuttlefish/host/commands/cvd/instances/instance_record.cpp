/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/instances/instance_record.h"

#include <android-base/logging.h>
#include <fmt/format.h>

#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/cli/commands/host_tool_target.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/status_fetcher.h"
#include "host/commands/cvd/utils/common.h"

namespace cuttlefish {

namespace {
constexpr int BASE_ADB_PORT = 6520;
constexpr int BASE_INSTANCE_ID = 1;

void AddEnvironmentForInstance(Command& cmd, const LocalInstance& instance) {
  cmd.AddEnvironmentVariable("HOME", instance.home_directory());
  cmd.AddEnvironmentVariable(kAndroidHostOut, instance.host_artifacts_path());
  cmd.AddEnvironmentVariable(kAndroidSoongHostOut, instance.host_artifacts_path());
}

}  // namespace

LocalInstance::LocalInstance(std::shared_ptr<cvd::InstanceGroup> group_proto,
                             cvd::Instance* instance_proto)
    : group_proto_(group_proto), instance_proto_(instance_proto) {}

void LocalInstance::set_state(cvd::InstanceState state) {
  instance_proto_->set_state(state);
}

std::string LocalInstance::instance_dir() const {
  return fmt::format("{}/cuttlefish/instances/cvd-{}",
                     group_proto_->home_directory(), id());
}

int LocalInstance::adb_port() const {
  // The instance id is zero for a very short time between the load and create
  // commands. The adb_port property should not be accessed during that time,
  // but return an invalid port number just in case.
  if (id() == 0) {
    return 0;
  }
  // run_cvd picks this port from the instance id and doesn't provide a flag
  // to change in cvd_internal_flag
  return BASE_ADB_PORT + id() - BASE_INSTANCE_ID;
}

std::string LocalInstance::assembly_dir() const {
  return AssemblyDirFromHome(home_directory());
}

bool LocalInstance::IsActive() const {
  switch (state()) {
    case cvd::INSTANCE_STATE_RUNNING:
    case cvd::INSTANCE_STATE_STARTING:
    case cvd::INSTANCE_STATE_STOPPING:
    case cvd::INSTANCE_STATE_PREPARING:
    case cvd::INSTANCE_STATE_UNREACHABLE:
      return true;
    case cvd::INSTANCE_STATE_UNSPECIFIED:
    case cvd::INSTANCE_STATE_STOPPED:
    case cvd::INSTANCE_STATE_PREPARE_FAILED:
    case cvd::INSTANCE_STATE_BOOT_FAILED:
    case cvd::INSTANCE_STATE_CANCELLED:
      return false;
    // Include these just to avoid the warning
    default:
      LOG(FATAL) << "Invalid instance state: " << state();
  }
  return false;
}

Result<Json::Value> LocalInstance::FetchStatus(std::chrono::seconds timeout) {
  return CF_EXPECT(FetchInstanceStatus(*this, timeout));
}

Result<void> LocalInstance::PressPowerBtn() {
  Command cmd(
      CF_EXPECT(HostToolTarget(host_artifacts_path()).GetPowerBtnBinPath()));

  cmd.AddParameter("--instance_num=", id());
  cmd.SetEnvironment({});
  AddEnvironmentForInstance(cmd, *this);

  LOG(DEBUG) << "Executing: " << cmd.ToString();

  siginfo_t infop;
  cmd.Start().Wait(&infop, WEXITED);
  CF_EXPECT(CheckProcessExitedNormally(infop));

  return {};
}

Result<void> LocalInstance::Restart(std::chrono::seconds launcher_timeout,
                     std::chrono::seconds boot_timeout) {
  Command cmd(
      CF_EXPECT(HostToolTarget(host_artifacts_path()).GetRestartBinPath()));

  cmd.AddParameter("-wait_for_launcher=", launcher_timeout.count());
  cmd.AddParameter("-boot_timeout=", boot_timeout.count());
  cmd.AddParameter("--undefok=wait_for_launcher,boot_timeout");

  cmd.AddParameter("--instance_num=", id());
  cmd.SetEnvironment({});
  AddEnvironment(cmd, *this);

  LOG(DEBUG) << "Executing: " << cmd.ToString();

  siginfo_t infop;
  cmd.Start().Wait(&infop, WEXITED);
  CF_EXPECT(CheckProcessExitedNormally(infop));

  return {};
}

Result<void> LocalInstance::PowerWash(std::chrono::seconds launcher_timeout,
                       std::chrono::seconds boot_timeout) {
  Command cmd(
      CF_EXPECT(HostToolTarget(host_artifacts_path()).GetPowerwashBinPath()));

  cmd.AddParameter("-wait_for_launcher=", launcher_timeout.count());
  cmd.AddParameter("-boot_timeout=", boot_timeout.count());
  cmd.AddParameter("--undefok=wait_for_launcher,boot_timeout");

  cmd.AddParameter("--instance_num=", id());
  cmd.SetEnvironment({});
  AddEnvironment(cmd, *this);

  LOG(DEBUG) << "Executing: " << cmd.ToString();

  siginfo_t infop;
  cmd.Start().Wait(&infop, WEXITED);
  CF_EXPECT(CheckProcessExitedNormally(infop));

  return {};
}

}  // namespace cuttlefish

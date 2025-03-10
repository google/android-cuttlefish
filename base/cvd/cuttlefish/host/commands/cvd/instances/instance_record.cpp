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

#include <fstream>

#include <android-base/logging.h>
#include <fmt/format.h>

#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/cli/commands/host_tool_target.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/commands/cvd/instances/status_fetcher.h"
#include "host/commands/cvd/utils/common.h"
#include "host/libs/command_util/runner/defs.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

namespace {
constexpr int BASE_ADB_PORT = 6520;
constexpr int BASE_INSTANCE_ID = 1;

void AddEnvironmentForInstance(Command& cmd, const LocalInstance& instance) {
  cmd.AddEnvironmentVariable("HOME", instance.home_directory());
  cmd.AddEnvironmentVariable(kAndroidHostOut, instance.host_artifacts_path());
  cmd.AddEnvironmentVariable(kAndroidSoongHostOut,
                             instance.host_artifacts_path());
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
  auto bin_check = 
      HostToolTarget(host_artifacts_path()).GetPowerBtnBinPath();
  if (bin_check.ok()) {
    return PressPowerBtnLegacy();
  }

  std::unique_ptr<const CuttlefishConfig> config = CuttlefishConfig::GetFromFile(instance_dir() + "/cuttlefish_config.json");
  CF_EXPECT_EQ(config->vm_manager(), VmmMode::kCrosvm,
               "powerbtn not supported in vm manager " << config->vm_manager());
  auto instance = config->ForInstance(id());

  Command command(instance.crosvm_binary());
  command.AddParameter("powerbtn");
  command.AddParameter(instance.CrosvmSocketPath());

  LOG(INFO) << "Pressing power button";
  std::string output;
  std::string error;
  auto ret = RunWithManagedStdio(std::move(command), NULL, &output, &error);
  CF_EXPECT_EQ(ret, 0,
               "crosvm powerbtn returned: " << ret << "\n"
                                            << output << "\n"
                                            << error);
  return {};
}

Result<void> LocalInstance::PressPowerBtnLegacy() {
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
  SharedFD monitor_socket = CF_EXPECT(GetLauncherMonitor(launcher_timeout));

  LOG(INFO) << "Requesting restart";
  CF_EXPECT(RunLauncherAction(monitor_socket, LauncherAction::kRestart,
                              launcher_timeout.count()));

  LOG(INFO) << "Waiting for device to boot up again";
  CF_EXPECT(WaitForRead(monitor_socket, boot_timeout.count()));
  RunnerExitCodes boot_exit_code = CF_EXPECT(ReadExitCode(monitor_socket));
  CF_EXPECT(boot_exit_code != RunnerExitCodes::kVirtualDeviceBootFailed,
            "Boot failed");
  CF_EXPECT(boot_exit_code == RunnerExitCodes::kSuccess,
            "Unknown response" << static_cast<int>(boot_exit_code));

  LOG(INFO) << "Restart successful";
  return {};
}

Result<void> LocalInstance::PowerWash(std::chrono::seconds launcher_timeout,
                                      std::chrono::seconds boot_timeout) {
  SharedFD monitor_socket = CF_EXPECT(GetLauncherMonitor(launcher_timeout));

  LOG(INFO) << "Requesting powerwash";
  CF_EXPECT(RunLauncherAction(monitor_socket, LauncherAction::kPowerwash,
                              launcher_timeout.count()));

  LOG(INFO) << "Waiting for device to boot up again";
  CF_EXPECT(WaitForRead(monitor_socket, boot_timeout.count()));
  RunnerExitCodes boot_exit_code = CF_EXPECT(ReadExitCode(monitor_socket));
  CF_EXPECT(boot_exit_code != RunnerExitCodes::kVirtualDeviceBootFailed,
            "Boot failed");
  CF_EXPECT(boot_exit_code == RunnerExitCodes::kSuccess,
            "Unknown response" << static_cast<int>(boot_exit_code));

  LOG(INFO) << "Powerwash successful";

  return {};
}

Result<Json::Value> LocalInstance::ReadJsonConfig() const {
  Json::CharReaderBuilder builder;
  std::string config_file = instance_dir() + "/cuttlefish_config.json";
  std::ifstream ifs(config_file);
  std::string errorMessage;
  Json::Value config;
  bool parsed = Json::parseFromStream(builder, ifs, &config, &errorMessage);
  CF_EXPECTF(std::move(parsed), "Could not read config file {}: {}",
             config_file, errorMessage);
  return config;
}

Result<SharedFD> LocalInstance::GetLauncherMonitor(
    std::chrono::seconds timeout) const {
  // Newer cuttlefish instances put launcher monitor socket in a directory
  // under /tmp, and store this path in the config. Older instances just put
  // them in the instance directory.
  std::string uds_dir = instance_dir();
  Json::Value config = CF_EXPECT(ReadJsonConfig());
  if (config.isMember("instances_uds_dir") &&
      config["instances_uds_dir"].isString()) {
    uds_dir = fmt::format("{}/cvd-{}", config["instances_uds_dir"].asString(), id());
  }
  std::string monitor_path = uds_dir + "/launcher_monitor.sock";
  SharedFD monitor = SharedFD::SocketLocalClient(monitor_path, false,
                                                 SOCK_STREAM, timeout.count());
  CF_EXPECTF(monitor->IsOpen(),
             "Failed to connect to instance monitor socket ({}): {}",
             monitor_path, monitor->StrError());
  return monitor;
}

}  // namespace cuttlefish

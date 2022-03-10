/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/instance_manager.h"

#include <map>
#include <mutex>
#include <optional>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

std::optional<std::string> GetCuttlefishConfigPath(
    const std::string& assembly_dir) {
  std::string assembly_dir_realpath;
  if (DirectoryExists(assembly_dir)) {
    CHECK(android::base::Realpath(assembly_dir, &assembly_dir_realpath));
    std::string config_path =
        AbsolutePath(assembly_dir_realpath + "/" + "cuttlefish_config.json");
    if (FileExists(config_path)) {
      return config_path;
    }
  }
  return {};
}

bool InstanceManager::HasAssemblies() const {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  return !assemblies_.empty();
}

void InstanceManager::SetAssembly(const InstanceManager::AssemblyDir& dir,
                                  const InstanceManager::AssemblyInfo& info) {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  assemblies_[dir] = info;
}

Result<InstanceManager::AssemblyInfo> InstanceManager::GetAssembly(
    const InstanceManager::AssemblyDir& dir) const {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  auto info_it = assemblies_.find(dir);
  if (info_it == assemblies_.end()) {
    return CF_ERR("No assembly dir \"" << dir << "\"");
  } else {
    return info_it->second;
  }
}

cvd::Status InstanceManager::CvdFleet(const SharedFD& out,
                                      const std::string& env_config) const {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  for (const auto& it : assemblies_) {
    const AssemblyDir& assembly_dir = it.first;
    const AssemblyInfo& assembly_info = it.second;
    auto config_path = GetCuttlefishConfigPath(assembly_dir);
    if (FileExists(env_config)) {
      config_path = env_config;
    }
    if (config_path) {
      // Reads CuttlefishConfig::instance_names(), which must remain stable
      // across changes to config file format (within server_constants.h major
      // version).
      auto config = CuttlefishConfig::GetFromFile(*config_path);
      if (config) {
        for (const std::string& instance_name : config->instance_names()) {
          Command command(assembly_info.host_binaries_dir + kStatusBin);
          command.AddParameter("--print");
          command.AddParameter("--instance_name=", instance_name);
          command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
          command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                                         *config_path);
          if (int wait_result = command.Start().Wait(); wait_result != 0) {
            WriteAll(out, "      (unknown instance status error)");
          }
        }
      }
    }
  }
  cvd::Status status;
  status.set_code(cvd::Status::OK);
  return status;
}

cvd::Status InstanceManager::CvdClear(const SharedFD& out,
                                      const SharedFD& err) {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  cvd::Status status;
  for (const auto& it : assemblies_) {
    const AssemblyDir& assembly_dir = it.first;
    const AssemblyInfo& assembly_info = it.second;
    auto config_path = GetCuttlefishConfigPath(assembly_dir);
    if (config_path) {
      // Stop all instances that are using this assembly dir.
      Command command(assembly_info.host_binaries_dir + kStopBin);
      // Delete the instance dirs.
      command.AddParameter("--clear_instance_dirs");
      command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
      command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
      command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, *config_path);
      if (int wait_result = command.Start().Wait(); wait_result != 0) {
        WriteAll(out,
                 "Warning: error stopping instances for assembly dir " +
                     assembly_dir +
                     ".\nThis can happen if instances are already stopped.\n");
      }

      // Delete the assembly dir.
      WriteAll(out, "Deleting " + assembly_dir + "\n");
      if (DirectoryExists(assembly_dir) &&
          !RecursivelyRemoveDirectory(assembly_dir)) {
        status.set_code(cvd::Status::FAILED_PRECONDITION);
        status.set_message("Unable to rmdir " + assembly_dir);
        return status;
      }
    }
  }
  RemoveFile(StringFromEnv("HOME", ".") + "/cuttlefish_runtime");
  RemoveFile(GetGlobalConfigFileLink());
  WriteAll(out,
           "Stopped all known instances and deleted all "
           "known assembly and instance dirs.\n");

  assemblies_.clear();
  status.set_code(cvd::Status::OK);
  return status;
}

}  // namespace cuttlefish

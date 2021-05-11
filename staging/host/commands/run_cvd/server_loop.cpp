/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "host/commands/run_cvd/server_loop.h"

#include <gflags/gflags.h>
#include <unistd.h>
#include <string>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"

namespace cuttlefish {

namespace {

bool CreateQcowOverlay(const std::string& crosvm_path,
                       const std::string& backing_file,
                       const std::string& output_overlay_path) {
  Command crosvm_qcow2_cmd(crosvm_path);
  crosvm_qcow2_cmd.AddParameter("create_qcow2");
  crosvm_qcow2_cmd.AddParameter("--backing_file=", backing_file);
  crosvm_qcow2_cmd.AddParameter(output_overlay_path);
  int success = crosvm_qcow2_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run crosvm create_qcow2. Exited with status "
               << success;
    return false;
  }
  return true;
}

void DeleteFifos(const CuttlefishConfig::InstanceSpecific& instance) {
  // TODO(schuffelen): Create these FIFOs in assemble_cvd instead of run_cvd.
  std::vector<std::string> pipes = {
      instance.kernel_log_pipe_name(),
      instance.console_in_pipe_name(),
      instance.console_out_pipe_name(),
      instance.logcat_pipe_name(),
      instance.PerInstanceInternalPath("keymaster_fifo_vm.in"),
      instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
      instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
      instance.PerInstanceInternalPath("bt_fifo_vm.in"),
      instance.PerInstanceInternalPath("bt_fifo_vm.out"),
  };
  for (const auto& pipe : pipes) {
    unlink(pipe.c_str());
  }
}

bool PowerwashFiles() {
  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Could not load the config.";
    return false;
  }
  auto instance = config->ForDefaultInstance();

  DeleteFifos(instance);

  // TODO(schuffelen): Clean up duplication with assemble_cvd
  auto kregistry_path = instance.access_kregistry_path();
  unlink(kregistry_path.c_str());
  CreateBlankImage(kregistry_path, 2 /* mb */, "none");

  auto pstore_path = instance.pstore_path();
  unlink(pstore_path.c_str());
  CreateBlankImage(pstore_path, 2 /* mb */, "none");

  auto sdcard_path = instance.sdcard_path();
  auto sdcard_size = FileSize(sdcard_path);
  unlink(sdcard_path.c_str());
  // round up
  auto sdcard_mb_size = (sdcard_size + (1 << 20) - 1) / (1 << 20);
  LOG(DEBUG) << "Size in mb is " << sdcard_mb_size;
  CreateBlankImage(sdcard_path, sdcard_mb_size, "sdcard");

  auto overlay_path = instance.PerInstancePath("overlay.img");
  unlink(overlay_path.c_str());
  if (!CreateQcowOverlay(config->crosvm_binary(),
                         instance.os_composite_disk_path(), overlay_path)) {
    LOG(ERROR) << "CreateQcowOverlay failed";
    return false;
  }
  return true;
}

void RestartRunCvd(const CuttlefishConfig& config, int notification_fd) {
  auto config_path = config.AssemblyPath("cuttlefish_config.json");
  auto followup_stdin = SharedFD::MemfdCreate("pseudo_stdin");
  WriteAll(followup_stdin, config_path + "\n");
  followup_stdin->LSeek(0, SEEK_SET);
  followup_stdin->UNMANAGED_Dup2(0);

  auto argv_vec = gflags::GetArgvs();
  char** argv = new char*[argv_vec.size() + 2];
  for (size_t i = 0; i < argv_vec.size(); i++) {
    argv[i] = argv_vec[i].data();
  }
  // Will take precedence over any earlier arguments.
  std::string reboot_notification =
      "-reboot_notification_fd=" + std::to_string(notification_fd);
  argv[argv_vec.size()] = reboot_notification.data();
  argv[argv_vec.size() + 1] = nullptr;

  execv("/proc/self/exe", argv);
  // execve should not return, so something went wrong.
  PLOG(ERROR) << "execv returned: ";
}

}  // namespace

void ServerLoop(SharedFD server, ProcessMonitor* process_monitor) {
  while (true) {
    // TODO: use select to handle simultaneous connections.
    auto client = SharedFD::Accept(*server);
    LauncherAction action;
    while (client->IsOpen() && client->Read(&action, sizeof(action)) > 0) {
      switch (action) {
        case LauncherAction::kStop:
          if (process_monitor->StopMonitoredProcesses()) {
            auto response = LauncherResponse::kSuccess;
            client->Write(&response, sizeof(response));
            std::exit(0);
          } else {
            auto response = LauncherResponse::kError;
            client->Write(&response, sizeof(response));
          }
          break;
        case LauncherAction::kStatus: {
          // TODO(schuffelen): Return more information on a side channel
          auto response = LauncherResponse::kSuccess;
          client->Write(&response, sizeof(response));
          break;
        }
        case LauncherAction::kPowerwash: {
          LOG(INFO) << "Received a Powerwash request from the monitor socket";
          if (!process_monitor->StopMonitoredProcesses()) {
            LOG(ERROR) << "Stopping processes failed.";
            auto response = LauncherResponse::kError;
            client->Write(&response, sizeof(response));
            break;
          }
          if (!PowerwashFiles()) {
            LOG(ERROR) << "Powerwashing files failed.";
            auto response = LauncherResponse::kError;
            client->Write(&response, sizeof(response));
            break;
          }
          auto response = LauncherResponse::kSuccess;
          client->Write(&response, sizeof(response));

          auto config = CuttlefishConfig::Get();
          CHECK(config) << "Could not load config";
          RestartRunCvd(*config, client->UNMANAGED_Dup());
          // RestartRunCvd should not return, so something went wrong.
          response = LauncherResponse::kError;
          client->Write(&response, sizeof(response));
          LOG(FATAL) << "run_cvd in a bad state";
          break;
        }
        case LauncherAction::kRestart: {
          if (!process_monitor->StopMonitoredProcesses()) {
            LOG(ERROR) << "Stopping processes failed.";
            auto response = LauncherResponse::kError;
            client->Write(&response, sizeof(response));
            break;
          }

          auto config = CuttlefishConfig::Get();
          CHECK(config) << "Could not load config";
          auto instance = config->ForDefaultInstance();
          DeleteFifos(instance);

          auto response = LauncherResponse::kSuccess;
          client->Write(&response, sizeof(response));
          CHECK(config) << "Could not load config";
          RestartRunCvd(*config, client->UNMANAGED_Dup());
          // RestartRunCvd should not return, so something went wrong.
          response = LauncherResponse::kError;
          client->Write(&response, sizeof(response));
          LOG(FATAL) << "run_cvd in a bad state";
          break;
        }
        default:
          LOG(ERROR) << "Unrecognized launcher action: "
                     << static_cast<char>(action);
          auto response = LauncherResponse::kError;
          client->Write(&response, sizeof(response));
      }
    }
  }
}

}  // namespace cuttlefish

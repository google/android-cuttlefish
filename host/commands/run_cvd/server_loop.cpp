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

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>

#include <fruit/fruit.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/data_image.h"
#include "host/libs/config/feature.h"
#include "host/libs/config/inject.h"

namespace cuttlefish {

namespace {

bool CreateQcowOverlay(const std::string& crosvm_path,
                       const std::string& backing_file,
                       const std::string& output_overlay_path) {
  Command crosvm_qcow2_cmd(crosvm_path);
  crosvm_qcow2_cmd.AddParameter("create_qcow2");
  crosvm_qcow2_cmd.AddParameter("--backing-file");
  crosvm_qcow2_cmd.AddParameter(backing_file);
  crosvm_qcow2_cmd.AddParameter(output_overlay_path);
  int success = crosvm_qcow2_cmd.Start().Wait();
  if (success != 0) {
    LOG(ERROR) << "Unable to run crosvm create_qcow2. Exited with status "
               << success;
    return false;
  }
  return true;
}

class ServerLoopImpl : public ServerLoop,
                       public SetupFeature,
                       public LateInjected {
 public:
  INJECT(ServerLoopImpl(const CuttlefishConfig& config,
                        const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  Result<void> LateInject(fruit::Injector<>& injector) override {
    command_sources_ = injector.getMultibindings<CommandSource>();
    return {};
  }

  // ServerLoop
  Result<void> Run() override {
    // Monitor and restart host processes supporting the CVD
    ProcessMonitor::Properties process_monitor_properties;
    process_monitor_properties.RestartSubprocesses(
        instance_.restart_subprocesses());

    for (auto& command_source : command_sources_) {
      if (command_source->Enabled()) {
        auto commands = CF_EXPECT(command_source->Commands());
        process_monitor_properties.AddCommands(std::move(commands));
      }
    }

    ProcessMonitor process_monitor(std::move(process_monitor_properties));

    CF_EXPECT(process_monitor.StartAndMonitorProcesses());

    while (true) {
      // TODO: use select to handle simultaneous connections.
      auto client = SharedFD::Accept(*server_);
      LauncherAction action;
      while (client->IsOpen() && client->Read(&action, sizeof(action)) > 0) {
        switch (action) {
          case LauncherAction::kStop: {
            auto stop = process_monitor.StopMonitoredProcesses();
            if (stop.ok()) {
              auto response = LauncherResponse::kSuccess;
              client->Write(&response, sizeof(response));
              std::exit(0);
            } else {
              LOG(ERROR) << "Failed to stop subprocesses:\n"
                         << stop.error().Message();
              LOG(DEBUG) << "Failed to stop subprocesses:\n"
                         << stop.error().Trace();
              auto response = LauncherResponse::kError;
              client->Write(&response, sizeof(response));
            }
            break;
          }
          case LauncherAction::kStatus: {
            // TODO(schuffelen): Return more information on a side channel
            auto response = LauncherResponse::kSuccess;
            client->Write(&response, sizeof(response));
            break;
          }
          case LauncherAction::kPowerwash: {
            LOG(INFO) << "Received a Powerwash request from the monitor socket";
            const auto& disks = instance_.virtual_disk_paths();
            auto overlay = instance_.PerInstancePath("overlay.img");
            if (std::find(disks.begin(), disks.end(), overlay) == disks.end()) {
              LOG(ERROR) << "Powerwash unsupported with --use_overlay=false";
              auto response = LauncherResponse::kError;
              client->Write(&response, sizeof(response));
              break;
            }

            auto stop = process_monitor.StopMonitoredProcesses();
            if (!stop.ok()) {
              LOG(ERROR) << "Stopping processes failed:\n"
                         << stop.error().Message();
              LOG(DEBUG) << "Stopping processes failed:\n"
                         << stop.error().Trace();
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

            RestartRunCvd(client->UNMANAGED_Dup());
            // RestartRunCvd should not return, so something went wrong.
            response = LauncherResponse::kError;
            client->Write(&response, sizeof(response));
            LOG(FATAL) << "run_cvd in a bad state";
            break;
          }
          case LauncherAction::kRestart: {
            auto stop = process_monitor.StopMonitoredProcesses();
            if (!stop.ok()) {
              LOG(ERROR) << "Stopping processes failed:\n"
                         << stop.error().Message();
              LOG(DEBUG) << "Stopping processes failed:\n"
                         << stop.error().Trace();
              auto response = LauncherResponse::kError;
              client->Write(&response, sizeof(response));
              break;
            }
            DeleteFifos();

            auto response = LauncherResponse::kSuccess;
            client->Write(&response, sizeof(response));
            RestartRunCvd(client->UNMANAGED_Dup());
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

  // SetupFeature
  std::string Name() const override { return "ServerLoop"; }

 private:
  bool Enabled() const override { return true; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() {
    auto launcher_monitor_path = instance_.launcher_monitor_socket_path();
    server_ = SharedFD::SocketLocalServer(launcher_monitor_path.c_str(), false,
                                          SOCK_STREAM, 0666);
    if (!server_->IsOpen()) {
      LOG(ERROR) << "Error when opening launcher server: "
                 << server_->StrError();
      return false;
    }
    return true;
  }

  void DeleteFifos() {
    // TODO(schuffelen): Create these FIFOs in assemble_cvd instead of run_cvd.
    std::vector<std::string> pipes = {
        instance_.kernel_log_pipe_name(),
        instance_.console_in_pipe_name(),
        instance_.console_out_pipe_name(),
        instance_.logcat_pipe_name(),
        instance_.PerInstanceInternalPath("keymaster_fifo_vm.in"),
        instance_.PerInstanceInternalPath("keymaster_fifo_vm.out"),
        instance_.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
        instance_.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
        instance_.PerInstanceInternalPath("bt_fifo_vm.in"),
        instance_.PerInstanceInternalPath("bt_fifo_vm.out"),
        instance_.PerInstanceInternalPath("gnsshvc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("gnsshvc_fifo_vm.out"),
        instance_.PerInstanceInternalPath("locationhvc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("locationhvc_fifo_vm.out"),
        instance_.PerInstanceInternalPath("confui_fifo_vm.in"),
        instance_.PerInstanceInternalPath("confui_fifo_vm.out"),
    };
    for (const auto& pipe : pipes) {
      unlink(pipe.c_str());
    }
  }

  bool PowerwashFiles() {
    DeleteFifos();

    // TODO(b/269669405): Figure out why this file is not being deleted
    unlink(instance_.PerInstanceInternalPath("crosvm_control.sock").c_str());

    // TODO(schuffelen): Clean up duplication with assemble_cvd
    unlink(instance_.PerInstancePath("NVChip").c_str());

    auto kregistry_path = instance_.access_kregistry_path();
    unlink(kregistry_path.c_str());
    CreateBlankImage(kregistry_path, 2 /* mb */, "none");

    auto hwcomposer_pmem_path = instance_.hwcomposer_pmem_path();
    unlink(hwcomposer_pmem_path.c_str());
    CreateBlankImage(hwcomposer_pmem_path, 2 /* mb */, "none");

    auto pstore_path = instance_.pstore_path();
    unlink(pstore_path.c_str());
    CreateBlankImage(pstore_path, 2 /* mb */, "none");

    auto sdcard_path = instance_.sdcard_path();
    auto sdcard_size = FileSize(sdcard_path);
    unlink(sdcard_path.c_str());
    // round up
    auto sdcard_mb_size = (sdcard_size + (1 << 20) - 1) / (1 << 20);
    LOG(DEBUG) << "Size in mb is " << sdcard_mb_size;
    CreateBlankImage(sdcard_path, sdcard_mb_size, "sdcard");

    struct OverlayFile {
      std::string name;
      std::string composite_disk_path;

      OverlayFile(std::string name, std::string composite_disk_path)
          : name(std::move(name)), composite_disk_path(std::move(composite_disk_path)) {}
    };
    std::vector<OverlayFile> overlay_files{
      OverlayFile("overlay.img", instance_.os_composite_disk_path())
    };
    if (instance_.ap_boot_flow() != CuttlefishConfig::InstanceSpecific::APBootFlow::None) {
      overlay_files.emplace_back(
        OverlayFile("ap_overlay.img", instance_.ap_composite_disk_path()));
    }
    for (const auto& overlay_file : overlay_files) {
      auto overlay_path = instance_.PerInstancePath(overlay_file.name.c_str());
      auto composite_disk_path = overlay_file.composite_disk_path.c_str();

      unlink(overlay_path.c_str());
      if (!CreateQcowOverlay(instance_.crosvm_binary(), composite_disk_path, overlay_path)) {
        LOG(ERROR) << "CreateQcowOverlay failed";
        return false;
      }
    }
    return true;
  }

  void RestartRunCvd(int notification_fd) {
    auto config_path = config_.AssemblyPath("cuttlefish_config.json");
    auto followup_stdin = SharedFD::MemfdCreate("pseudo_stdin");
    WriteAll(followup_stdin, config_path + "\n");
    followup_stdin->LSeek(0, SEEK_SET);
    followup_stdin->UNMANAGED_Dup2(0);

    auto argv_vec = gflags::GetArgvs();
    std::unique_ptr<char*[]> argv(new char*[argv_vec.size() + 2]);
    for (size_t i = 0; i < argv_vec.size(); i++) {
      argv[i] = argv_vec[i].data();
    }
    // Will take precedence over any earlier arguments.
    std::string reboot_notification =
        "-reboot_notification_fd=" + std::to_string(notification_fd);
    argv[argv_vec.size()] = reboot_notification.data();
    argv[argv_vec.size() + 1] = nullptr;

    execv("/proc/self/exe", argv.get());
    // execve should not return, so something went wrong.
    PLOG(ERROR) << "execv returned: ";
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<CommandSource*> command_sources_;
  SharedFD server_;
};

}  // namespace

ServerLoop::~ServerLoop() = default;

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>,
                 ServerLoop>
serverLoopComponent() {
  return fruit::createComponent()
      .bind<ServerLoop, ServerLoopImpl>()
      .addMultibinding<LateInjected, ServerLoopImpl>()
      .addMultibinding<SetupFeature, ServerLoopImpl>();
}

}  // namespace cuttlefish

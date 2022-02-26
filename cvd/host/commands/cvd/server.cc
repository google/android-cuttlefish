/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/commands/cvd/server.h"

#include <signal.h>

#include <atomic>
#include <future>
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
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

static fruit::Component<> RequestComponent(CvdServer* server) {
  return fruit::createComponent()
      .bindInstance(*server)
      .install(cvdCommandComponent)
      .install(cvdShutdownComponent)
      .install(cvdVersionComponent);
}

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

CvdServer::CvdServer() : running_(true) {}

bool CvdServer::HasAssemblies() const {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  return !assemblies_.empty();
}

void CvdServer::SetAssembly(const CvdServer::AssemblyDir& dir,
                            const CvdServer::AssemblyInfo& info) {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  assemblies_[dir] = info;
}

Result<CvdServer::AssemblyInfo> CvdServer::GetAssembly(
    const CvdServer::AssemblyDir& dir) const {
  std::lock_guard assemblies_lock(assemblies_mutex_);
  auto info_it = assemblies_.find(dir);
  if (info_it == assemblies_.end()) {
    return CF_ERR("No assembly dir \"" << dir << "\"");
  } else {
    return info_it->second;
  }
}

void CvdServer::Stop() { running_ = false; }

static Result<CvdServerHandler*> RequestHandler(
    const RequestWithStdio& request,
    const std::vector<CvdServerHandler*>& handlers) {
  Result<cvd::Response> response;
  std::vector<CvdServerHandler*> compatible_handlers;
  for (auto& handler : handlers) {
    if (CF_EXPECT(handler->CanHandle(request))) {
      compatible_handlers.push_back(handler);
    }
  }
  CF_EXPECT(compatible_handlers.size() == 1,
            "Expected exactly one handler for message, found "
                << compatible_handlers.size());
  return compatible_handlers[0];
}

Result<void> CvdServer::ServerLoop(SharedFD server) {
  while (running_) {
    auto client_fd = SharedFD::Accept(*server);
    CF_EXPECT(client_fd->IsOpen(), client_fd->StrError());

    auto message_queue = CF_EXPECT(ClientMessageQueue::Create(client_fd));

    std::atomic_bool running = true;
    std::mutex handler_mutex;
    CvdServerHandler* handler = nullptr;
    std::thread message_responder([this, &message_queue, &handler,
                                   &handler_mutex, &running]() {
      while (running) {
        auto request = message_queue.WaitForRequest();
        if (!request.ok()) {
          LOG(DEBUG) << "Didn't get client request:"
                     << request.error().message();
          break;
        }
        fruit::Injector<> request_injector(RequestComponent, this);
        Result<cvd::Response> response;
        {
          std::scoped_lock lock(handler_mutex);
          auto handler_result = RequestHandler(
              *request, request_injector.getMultibindings<CvdServerHandler>());
          if (handler_result.ok()) {
            handler = *handler_result;
          } else {
            handler = nullptr;
            response = cvd::Response();
            response->mutable_status()->set_code(cvd::Status::INTERNAL);
            response->mutable_status()->set_message("No handler found");
          }
          // Drop the handler lock so it has a chance to be interrupted.
        }
        if (handler) {
          response = handler->Handle(*request);
        }
        {
          std::scoped_lock lock(handler_mutex);
          handler = nullptr;
        }
        if (!response.ok()) {
          LOG(DEBUG) << "Error handling request: " << request.error().message();
          cvd::Response error_response;
          error_response.mutable_status()->set_code(cvd::Status::INTERNAL);
          error_response.mutable_status()->set_message(
              response.error().message());
          response = error_response;
        }
        auto write_response = message_queue.PostResponse(*response);
        if (!write_response.ok()) {
          LOG(DEBUG) << "Error writing response: " << write_response.error();
          break;
        }
      }
    });

    message_queue.Join();
    // The client has gone away, do our best to interrupt/stop ongoing
    // operations.
    running = false;
    {
      // This might end up executing after the handler is completed, but it at
      // least won't race with the handler being overwritten by another pointer.
      std::scoped_lock lock(handler_mutex);
      if (handler) {
        auto interrupt = handler->Interrupt();
        if (!interrupt.ok()) {
          LOG(ERROR) << "Failed to interrupt handler: " << interrupt.error();
        }
      }
    }
    message_responder.join();
  }
  return {};
}

cvd::Status CvdServer::CvdFleet(const SharedFD& out,
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

cvd::Status CvdServer::CvdClear(const SharedFD& out, const SharedFD& err) {
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

static fruit::Component<CvdServer> ServerComponent() {
  return fruit::createComponent();
}

static Result<int> CvdServerMain(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  LOG(INFO) << "Starting server";

  signal(SIGPIPE, SIG_IGN);

  std::vector<Flag> flags;
  SharedFD server_fd;
  flags.emplace_back(
      SharedFDFlag("server_fd", server_fd)
          .Help("File descriptor to an already created vsock server"));
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CF_EXPECT(ParseFlags(flags, args));

  CF_EXPECT(server_fd->IsOpen(), "Did not receive a valid cvd_server fd");

  fruit::Injector<CvdServer> injector(ServerComponent);
  CF_EXPECT(injector.get<CvdServer&>().ServerLoop(server_fd));

  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::CvdServerMain(argc, argv);
  CHECK(res.ok()) << "cvd server failed: " << res.error().message();
  return *res;
}

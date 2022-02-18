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
#include "common/libs/utils/unix_sockets.h"
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

Result<void> CvdServer::AddHandler(CvdServerHandler* handler) {
  CF_EXPECT(handler != nullptr, "Received a null handler");
  handlers_.push_back(handler);
  return {};
}

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

void CvdServer::ServerLoop(const SharedFD& server) {
  while (running_) {
    SharedFDSet read_set;
    read_set.Set(server);
    int num_fds = Select(&read_set, nullptr, nullptr, nullptr);
    if (num_fds <= 0) {  // Ignore select error
      PLOG(ERROR) << "Select call returned error.";
    } else if (read_set.IsSet(server)) {
      auto client = SharedFD::Accept(*server);
      CHECK(client->IsOpen()) << "Failed to get client: " << client->StrError();
      while (true) {
        auto request = GetRequest(client);
        if (!request.ok()) {
          client->Close();
          break;
        }
        auto response = HandleRequest(*request);
        if (response.ok()) {
          auto resp_success = SendResponse(client, *response);
          if (!resp_success.ok()) {
            LOG(ERROR) << "Failed to write response: "
                       << resp_success.error().message();
            client->Close();
            break;
          }
        } else {
          LOG(ERROR) << response.error();
          cvd::Response error_response;
          error_response.mutable_status()->set_code(cvd::Status::INTERNAL);
          *error_response.mutable_status()->mutable_message() =
              response.error().message();
          auto resp_success = SendResponse(client, *response);
          if (!resp_success.ok()) {
            LOG(ERROR) << "Failed to write response: "
                       << resp_success.error().message();
          }
          client->Close();
          break;
        }
      }
    }
  }
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

Result<cvd::Response> CvdServer::HandleRequest(
    const RequestWithStdio& request) {
  Result<cvd::Response> response;
  std::vector<CvdServerHandler*> compatible_handlers;
  for (auto& handler : handlers_) {
    if (CF_EXPECT(handler->CanHandle(request))) {
      compatible_handlers.push_back(handler);
    }
  }
  CF_EXPECT(compatible_handlers.size() == 1,
            "Expected exactly one handler for message, found "
                << compatible_handlers.size());
  return CF_EXPECT(compatible_handlers[0]->Handle(request));
}

Result<UnixMessageSocket> CvdServer::GetClient(const SharedFD& client) const {
  UnixMessageSocket result(client);
  CF_EXPECT(result.EnableCredentials(true),
            "Unable to enable UnixMessageSocket credentials.");
  return result;
}

Result<RequestWithStdio> CvdServer::GetRequest(const SharedFD& client) const {
  RequestWithStdio result;

  UnixMessageSocket reader =
      CF_EXPECT(GetClient(client), "Couldn't get client");
  auto read_result = CF_EXPECT(reader.ReadMessage(), "Couldn't read message");

  CF_EXPECT(!read_result.data.empty(),
            "Read empty packet, so the client has probably closed "
            "the connection.");

  std::string serialized(read_result.data.begin(), read_result.data.end());
  cvd::Request request;
  CF_EXPECT(request.ParseFromString(serialized),
            "Unable to parse serialized request proto.");
  result.request = request;

  CF_EXPECT(read_result.HasFileDescriptors(),
            "Missing stdio fds from request.");
  auto fds = CF_EXPECT(read_result.FileDescriptors(),
                       "Error reading stdio fds from request");
  CF_EXPECT(fds.size() == 3 || fds.size() == 4, "Wrong number of FDs, received "
                                                    << fds.size()
                                                    << ", wanted 3 or 4");
  result.in = fds[0];
  result.out = fds[1];
  result.err = fds[2];
  if (fds.size() == 4) {
    result.extra = fds[3];
  }


  if (read_result.HasCredentials()) {
    // TODO(b/198453477): Use Credentials to control command access.
    auto creds =
        CF_EXPECT(read_result.Credentials(), "Failed to get credentials");
    LOG(DEBUG) << "Has credentials, uid=" << creds.uid;
  }

  return result;
}

Result<void> CvdServer::SendResponse(const SharedFD& client,
                                     const cvd::Response& response) const {
  std::string serialized;
  CF_EXPECT(response.SerializeToString(&serialized),
            "Unable to serialize response proto.");
  UnixSocketMessage message;
  message.data = std::vector<char>(serialized.begin(), serialized.end());

  UnixMessageSocket writer =
      CF_EXPECT(GetClient(client), "Couldn't get client");
  return writer.WriteMessage(message);
}

static fruit::Component<CvdServer> serverComponent() {
  return fruit::createComponent()
      .install(cvdCommandComponent)
      .install(cvdShutdownComponent)
      .install(cvdVersionComponent);
}

static Result<int> CvdServerMain(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  LOG(INFO) << "Starting server";

  std::vector<Flag> flags;
  SharedFD server_fd;
  flags.emplace_back(
      SharedFDFlag("server_fd", server_fd)
          .Help("File descriptor to an already created vsock server"));
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CF_EXPECT(ParseFlags(flags, args));

  CF_EXPECT(server_fd->IsOpen(), "Did not receive a valid cvd_server fd");

  fruit::Injector<CvdServer> injector(serverComponent);
  CvdServer& server = injector.get<CvdServer&>();
  for (auto handler : injector.getMultibindings<CvdServerHandler>()) {
    CF_EXPECT(server.AddHandler(handler));
  }

  server.ServerLoop(server_fd);
  return 0;
}

}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::CvdServerMain(argc, argv);
  CHECK(res.ok()) << "cvd server failed: " << res.error().message();
  return *res;
}

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
#include <optional>
#include <thread>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <build/version.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

using android::base::Error;

constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
constexpr char kStartBin[] = "cvd_internal_start";
constexpr char kStatusBin[] = "cvd_internal_status";
constexpr char kStopBin[] = "cvd_internal_stop";

constexpr char kClearBin[] = "clear_placeholder";  // Unused, runs CvdClear()
constexpr char kFleetBin[] = "fleet_placeholder";  // Unused, runs CvdFleet()
constexpr char kHelpBin[] = "help_placeholder";  // Unused, prints kHelpMessage.
constexpr char kHelpMessage[] = R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd <command> <args>

Commands:
  help                Print this message.
  help <command>      Print help for a command.
  start               Start a device.
  stop                Stop a running device.
  clear               Stop all running devices and delete all instance and assembly directories.
  fleet               View the current fleet status.
  kill-server         Kill the cvd_server background process.
  status              Check and print the state of a running instance.
  host_bugreport      Capture a host bugreport, including configs, logs, and tombstones.

Args:
  <command args>      Each command has its own set of args. See cvd help <command>.
  --clean             If provided, runs cvd kill-server before the requested command.
)";

const std::map<std::string, std::string> CommandToBinaryMap = {
    {"help", kHelpBin},
    {"host_bugreport", kHostBugreportBin},
    {"cvd_host_bugreport", kHostBugreportBin},
    {"start", kStartBin},
    {"launch_cvd", kStartBin},
    {"status", kStatusBin},
    {"cvd_status", kStatusBin},
    {"stop", kStopBin},
    {"stop_cvd", kStopBin},
    {"clear", kClearBin},
    {"fleet", kFleetBin}};

class CvdServer {
 public:
  void ServerLoop(const SharedFD& server) {
    while (running_) {
      SharedFDSet read_set;
      read_set.Set(server);
      int num_fds = Select(&read_set, nullptr, nullptr, nullptr);
      if (num_fds <= 0) {  // Ignore select error
        PLOG(ERROR) << "Select call returned error.";
      } else if (read_set.IsSet(server)) {
        auto client = SharedFD::Accept(*server);
        while (true) {
          android::base::Result<void> result = {};
          auto request_with_stdio = GetRequest(client);
          if (!request_with_stdio.ok()) {
            client->Close();
            break;
          }
          auto request = request_with_stdio->request;
          auto in = request_with_stdio->in;
          auto out = request_with_stdio->out;
          auto err = request_with_stdio->err;
          auto extra = request_with_stdio->extra;
          switch (request.contents_case()) {
            case cvd::Request::ContentsCase::CONTENTS_NOT_SET:
              // No more messages from this client.
              client->Close();
              break;
            case cvd::Request::ContentsCase::kVersionRequest:
              result = GetVersion(client);
              break;
            case cvd::Request::ContentsCase::kShutdownRequest:
              if (!extra) {
                result = Error()
                         << "Missing extra ShareFD for shutdown write_pipe";
              } else {
                result = Shutdown(client, request.shutdown_request(), out, err,
                                  *extra);
              }
              break;
            case cvd::Request::ContentsCase::kCommandRequest:
              result = HandleCommand(client, request.command_request(), in, out,
                                     err);
              break;
            default:
              result = Error() << "Unknown request in cvd_server.";
              break;
          }
          if (!result.ok()) {
            LOG(ERROR) << result.error();
            client->Close();
          }
        }
      }
    }
  }

  android::base::Result<void> GetVersion(const SharedFD& client) const {
    cvd::Response response;
    response.mutable_version_response()->mutable_version()->set_major(
        cvd::kVersionMajor);
    response.mutable_version_response()->mutable_version()->set_minor(
        cvd::kVersionMinor);
    response.mutable_version_response()->mutable_version()->set_build(
        android::build::GetBuildNumber());
    response.mutable_status()->set_code(cvd::Status::OK);
    return SendResponse(client, response);
  }

  android::base::Result<void> Shutdown(const SharedFD& client,
                                       const cvd::ShutdownRequest& request,
                                       const SharedFD& out, const SharedFD& err,
                                       const SharedFD& write_pipe) {
    cvd::Response response;
    response.mutable_shutdown_response();

    if (request.clear()) {
      *response.mutable_status() = CvdClear(out, err);
      if (response.status().code() != cvd::Status::OK) {
        return SendResponse(client, response);
      }
    }

    if (!assemblies_.empty()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Cannot shut down cvd_server while devices are being tracked. "
          "Try `cvd kill-server`.");
      return SendResponse(client, response);
    }

    // Intentionally leak the write_pipe fd so that it only closes
    // when this process fully exits.
    write_pipe->UNMANAGED_Dup();

    WriteAll(out, "Stopping the cvd_server.\n");
    running_ = false;
    response.mutable_status()->set_code(cvd::Status::OK);
    return SendResponse(client, response);
  }

  android::base::Result<void> HandleCommand(const SharedFD& client,
                                            const cvd::CommandRequest& request,
                                            const SharedFD& in,
                                            const SharedFD& out,
                                            const SharedFD& err) {
    cvd::Response response;
    response.mutable_command_response();

    if (request.args_size() == 0) {
      // No command to handle
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message("No args passed to HandleCommand");
      return SendResponse(client, response);
    }

    std::vector<Flag> flags;

    std::vector<std::string> args;
    for (const std::string& arg : request.args()) {
      args.push_back(arg);
    }

    std::string bin;
    std::string program_name = cpp_basename(args[0]);
    std::string subcommand_name = program_name;
    if (program_name == "cvd") {
      if (args.size() == 1) {
        // Show help if user invokes `cvd` alone.
        subcommand_name = "help";
      } else {
        subcommand_name = args[1];
      }
    }
    auto subcommand_bin = CommandToBinaryMap.find(subcommand_name);
    if (subcommand_bin == CommandToBinaryMap.end()) {
      // Show help if subcommand not found.
      bin = kHelpBin;
    } else {
      bin = subcommand_bin->second;
    }

    // Remove program name from args
    size_t args_to_skip = 1;
    if (program_name == "cvd" && args.size() > 1) {
      args_to_skip = 2;
    }
    args.erase(args.begin(), args.begin() + args_to_skip);

    // assembly_dir is used to possibly set CuttlefishConfig path env variable
    // later. This env variable is used by subcommands when locating the config.
    std::string assembly_dir =
        StringFromEnv("HOME", ".") + "/cuttlefish_assembly";
    flags.emplace_back(GflagsCompatFlag("assembly_dir", assembly_dir));

    // Create a copy of args before parsing, to be passed to subcommands.
    std::vector<std::string> args_copy = args;

    CHECK(ParseFlags(flags, args));

    auto host_artifacts_path = request.env().find("ANDROID_HOST_OUT");
    if (host_artifacts_path == request.env().end()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Missing ANDROID_HOST_OUT in client environment.");
      return SendResponse(client, response);
    }

    if (bin == kHelpBin) {
      // Handle `cvd help`
      if (args.empty()) {
        WriteAll(out, kHelpMessage);
        response.mutable_status()->set_code(cvd::Status::OK);
        return SendResponse(client, response);
      }

      // Certain commands have no detailed help text.
      std::set<std::string> builtins = {"help", "clear", "kill-server"};
      auto it = CommandToBinaryMap.find(args[0]);
      if (it == CommandToBinaryMap.end() ||
          builtins.find(args[0]) != builtins.end()) {
        WriteAll(out, kHelpMessage);
        response.mutable_status()->set_code(cvd::Status::OK);
        return SendResponse(client, response);
      }

      // Handle `cvd help <subcommand>` by calling the subcommand with --help.
      bin = it->second;
      args_copy.push_back("--help");
    } else if (bin == kClearBin) {
      *response.mutable_status() = CvdClear(out, err);
      return SendResponse(client, response);
    } else if (bin == kFleetBin) {
      *response.mutable_status() = CvdFleet(out);
      return SendResponse(client, response);
    } else if (bin == kStartBin) {
      // Track this assembly_dir in the fleet.
      AssemblyInfo info;
      info.host_binaries_dir = host_artifacts_path->second + "/bin/";
      assemblies_.emplace(assembly_dir, info);
    }

    Command command(assemblies_[assembly_dir].host_binaries_dir + bin);
    for (const std::string& arg : args_copy) {
      command.AddParameter(arg);
    }

    // Set CuttlefishConfig path based on assembly dir,
    // used by subcommands when locating the CuttlefishConfig.
    if (request.env().count(kCuttlefishConfigEnvVarName) == 0) {
      auto config_path = GetCuttlefishConfigPath(assembly_dir);
      if (config_path) {
        command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                                       *config_path);
      }
    }
    for (auto& it : request.env()) {
      command.AddEnvironmentVariable(it.first, it.second);
    }

    // Redirect stdin, stdout, stderr back to the cvd client
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, in);
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, out);
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, err);
    SubprocessOptions options;
    options.ExitWithParent(false);
    command.Start(options);

    response.mutable_status()->set_code(cvd::Status::OK);
    return SendResponse(client, response);
  }

 private:
  using AssemblyDir = std::string;
  struct AssemblyInfo {
    std::string host_binaries_dir;
  };
  std::map<AssemblyDir, AssemblyInfo> assemblies_;
  bool running_ = true;

  struct RequestWithStdio {
    cvd::Request request;
    SharedFD in, out, err;
    std::optional<SharedFD> extra;
  };

  std::optional<std::string> GetCuttlefishConfigPath(
      const std::string& assembly_dir) const {
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

  UnixMessageSocket GetClient(const SharedFD& client) const {
    UnixMessageSocket result = UnixMessageSocket(client);
    CHECK(result.EnableCredentials(true).ok())
        << "Unable to enable UnixMessageSocket credentials.";
    return result;
  }

  android::base::Result<RequestWithStdio> GetRequest(
      const SharedFD& client) const {
    RequestWithStdio result;

    UnixMessageSocket reader = GetClient(client);
    auto read_result = reader.ReadMessage();
    if (!read_result.ok()) {
      return Error() << read_result.error();
    }

    if (read_result->data.empty()) {
      return Error() << "Read empty packet, so the client has probably closed "
                        "the connection.";
    }

    std::string serialized(read_result->data.begin(), read_result->data.end());
    cvd::Request request;
    if (!request.ParseFromString(serialized)) {
      return Error() << "Unable to parse serialized request proto.";
    }
    result.request = request;

    if (!read_result->HasFileDescriptors()) {
      return Error() << "Missing stdio fds from request.";
    }
    auto fds = read_result->FileDescriptors();
    if (!fds.ok() || (fds->size() != 3 && fds->size() != 4)) {
      return Error() << "Error reading stdio fds from request: " << fds.error();
    }
    result.in = (*fds)[0];
    result.out = (*fds)[1];
    result.err = (*fds)[2];
    if (fds->size() == 4) {
      result.extra = (*fds)[3];
    }

    if (read_result->HasCredentials()) {
      // TODO(b/198453477): Use Credentials to control command access.
      LOG(DEBUG) << "Has credentials, uid=" << read_result->Credentials()->uid;
    }

    return result;
  }

  android::base::Result<void> SendResponse(
      const SharedFD& client, const cvd::Response& response) const {
    std::string serialized;
    if (!response.SerializeToString(&serialized)) {
      return android::base::Error() << "Unable to serialize response proto.";
    }
    UnixSocketMessage message;
    message.data = std::vector<char>(serialized.begin(), serialized.end());

    UnixMessageSocket writer = GetClient(client);
    return writer.WriteMessage(message);
  }

  cvd::Status CvdClear(const SharedFD& out, const SharedFD& err) {
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
        command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                                       *config_path);
        if (int wait_result = command.Start().Wait(); wait_result != 0) {
          WriteAll(
              out,
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

  cvd::Status CvdFleet(const SharedFD& out) const {
    for (const auto& it : assemblies_) {
      const AssemblyDir& assembly_dir = it.first;
      const AssemblyInfo& assembly_info = it.second;
      auto config_path = GetCuttlefishConfigPath(assembly_dir);
      if (config_path) {
        // Reads CuttlefishConfig::instance_names(), which must remain stable
        // across changes to config file format (within server.h major version).
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
};

int CvdServerMain(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);

  std::vector<Flag> flags;
  SharedFD server_fd;
  flags.emplace_back(
      SharedFDFlag("server_fd", server_fd)
          .Help("File descriptor to an already created vsock server"));
  std::vector<std::string> args =
      ArgsToVec(argc - 1, argv + 1);  // Skip argv[0]
  CHECK(ParseFlags(flags, args));

  CHECK(server_fd->IsOpen()) << "Did not receive a valid cvd_server fd";
  CvdServer server;
  server.ServerLoop(server_fd);
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  return cuttlefish::CvdServerMain(argc, argv);
}

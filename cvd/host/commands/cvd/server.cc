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
#include "common/libs/utils/result.h"
#include "common/libs/utils/shared_fd_flag.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/unix_sockets.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

using android::base::Error;

constexpr char kHostBugreportBin[] = "cvd_internal_host_bugreport";
constexpr char kStartBin[] = "cvd_internal_start";
constexpr char kStatusBin[] = "cvd_internal_status";
constexpr char kStopBin[] = "cvd_internal_stop";
constexpr char kSendSmsBin[] = "cvd_send_sms";

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
  send_sms            Send an SMS to a device.
  cvd_send_sms        Send an SMS to a device.

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
    {"fleet", kFleetBin},
    {"send_sms", kSendSmsBin},
    {"cvd_send_sms", kSendSmsBin},
};

struct RequestWithStdio {
  cvd::Request request;
  SharedFD in, out, err;
  std::optional<SharedFD> extra;
};

class CvdServerHandler {
 public:
  virtual ~CvdServerHandler() = default;

  virtual Result<bool> CanHandle(const RequestWithStdio&) const = 0;
  virtual Result<cvd::Response> Handle(const RequestWithStdio&) = 0;
};

static std::optional<std::string> GetCuttlefishConfigPath(
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

class CvdServer {
 public:
  using AssemblyDir = std::string;
  struct AssemblyInfo {
    std::string host_binaries_dir;
  };

  Result<void> AddHandler(CvdServerHandler* handler) {
    CF_EXPECT(handler != nullptr, "Received a null handler");
    handlers_.push_back(handler);
    return {};
  }

  std::map<AssemblyDir, AssemblyInfo>& Assemblies() { return assemblies_; }

  void Stop() { running_ = false; }

  void ServerLoop(const SharedFD& server) {
    while (running_) {
      SharedFDSet read_set;
      read_set.Set(server);
      int num_fds = Select(&read_set, nullptr, nullptr, nullptr);
      if (num_fds <= 0) {  // Ignore select error
        PLOG(ERROR) << "Select call returned error.";
      } else if (read_set.IsSet(server)) {
        auto client = SharedFD::Accept(*server);
        CHECK(client->IsOpen())
            << "Failed to get client: " << client->StrError();
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

 private:
  std::map<AssemblyDir, AssemblyInfo> assemblies_;
  std::vector<CvdServerHandler*> handlers_;
  bool running_ = true;

  Result<cvd::Response> HandleRequest(const RequestWithStdio& request) {
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

  Result<UnixMessageSocket> GetClient(const SharedFD& client) const {
    UnixMessageSocket result(client);
    CF_EXPECT(result.EnableCredentials(true),
              "Unable to enable UnixMessageSocket credentials.");
    return result;
  }

  Result<RequestWithStdio> GetRequest(const SharedFD& client) const {
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
    CF_EXPECT(
        fds.size() == 3 || fds.size() == 4,
        "Wrong number of FDs, received " << fds.size() << ", wanted 3 or 4");
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

  Result<void> SendResponse(const SharedFD& client,
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
};

class CvdVersionHandler : public CvdServerHandler {
 public:
  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return request.request.contents_case() ==
           cvd::Request::ContentsCase::kVersionRequest;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_version_response()->mutable_version()->set_major(
        cvd::kVersionMajor);
    response.mutable_version_response()->mutable_version()->set_minor(
        cvd::kVersionMinor);
    response.mutable_version_response()->mutable_version()->set_build(
        android::build::GetBuildNumber());
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }
};

class CvdShutdownHandler : public CvdServerHandler {
 public:
  CvdShutdownHandler(CvdServer& server) : server_(server) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return request.request.contents_case() ==
           cvd::Request::ContentsCase::kShutdownRequest;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_shutdown_response();

    if (!request.extra) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Missing extra SharedFD for shutdown");
      return response;
    }

    if (request.request.shutdown_request().clear()) {
      *response.mutable_status() = server_.CvdClear(request.out, request.err);
      return response;
    }

    if (!server_.Assemblies().empty()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Cannot shut down cvd_server while devices are being tracked. "
          "Try `cvd kill-server`.");
      return response;
    }

    // Intentionally leak the extra fd so that it only closes
    // when this process fully exits.
    (*request.extra)->UNMANAGED_Dup();

    WriteAll(request.out, "Stopping the cvd_server.\n");
    server_.Stop();

    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

 private:
  CvdServer& server_;
};

class CvdCommandHandler : public CvdServerHandler {
 public:
  CvdCommandHandler(CvdServer& server) : server_(server) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    return request.request.contents_case() ==
           cvd::Request::ContentsCase::kCommandRequest;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) {
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    response.mutable_command_response();

    if (request.request.command_request().args_size() == 0) {
      // No command to handle
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message("No args passed to HandleCommand");
      return response;
    }

    std::vector<Flag> flags;

    std::vector<std::string> args;
    for (const std::string& arg : request.request.command_request().args()) {
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

    auto host_artifacts_path =
        request.request.command_request().env().find("ANDROID_HOST_OUT");
    if (host_artifacts_path == request.request.command_request().env().end()) {
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Missing ANDROID_HOST_OUT in client environment.");
      return response;
    }

    if (bin == kHelpBin) {
      // Handle `cvd help`
      if (args.empty()) {
        WriteAll(request.out, kHelpMessage);
        response.mutable_status()->set_code(cvd::Status::OK);
        return response;
      }

      // Certain commands have no detailed help text.
      std::set<std::string> builtins = {"help", "clear", "kill-server"};
      auto it = CommandToBinaryMap.find(args[0]);
      if (it == CommandToBinaryMap.end() ||
          builtins.find(args[0]) != builtins.end()) {
        WriteAll(request.out, kHelpMessage);
        response.mutable_status()->set_code(cvd::Status::OK);
        return response;
      }

      // Handle `cvd help <subcommand>` by calling the subcommand with --help.
      bin = it->second;
      args_copy.push_back("--help");
    } else if (bin == kClearBin) {
      *response.mutable_status() = server_.CvdClear(request.out, request.err);
      return response;
    } else if (bin == kFleetBin) {
      auto env_config = request.request.command_request().env().find(
          kCuttlefishConfigEnvVarName);
      std::string config_path;
      if (env_config != request.request.command_request().env().end()) {
        config_path = env_config->second;
      }
      *response.mutable_status() = CvdFleet(request.out, config_path);
      return response;
    } else if (bin == kStartBin) {
      // Track this assembly_dir in the fleet.
      CvdServer::AssemblyInfo info;
      info.host_binaries_dir = host_artifacts_path->second + "/bin/";
      server_.Assemblies().emplace(assembly_dir, info);
    }

    Command command(server_.Assemblies()[assembly_dir].host_binaries_dir + bin);
    for (const std::string& arg : args_copy) {
      command.AddParameter(arg);
    }

    // Set CuttlefishConfig path based on assembly dir,
    // used by subcommands when locating the CuttlefishConfig.
    if (request.request.command_request().env().count(
            kCuttlefishConfigEnvVarName) == 0) {
      auto config_path = GetCuttlefishConfigPath(assembly_dir);
      if (config_path) {
        command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName,
                                       *config_path);
      }
    }
    for (auto& it : request.request.command_request().env()) {
      command.AddEnvironmentVariable(it.first, it.second);
    }

    // Redirect stdin, stdout, stderr back to the cvd client
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, request.in);
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, request.out);
    command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, request.err);
    SubprocessOptions options;
    options.ExitWithParent(false);
    command.Start(options);

    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

 private:
  CvdServer& server_;

  cvd::Status CvdFleet(const SharedFD& out,
                       const std::string& env_config) const {
    for (const auto& it : server_.Assemblies()) {
      const CvdServer::AssemblyDir& assembly_dir = it.first;
      const CvdServer::AssemblyInfo& assembly_info = it.second;
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
};

Result<int> CvdServerMain(int argc, char** argv) {
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

  CvdServer server;

  CvdVersionHandler version_handler;
  CF_EXPECT(server.AddHandler(&version_handler));

  CvdShutdownHandler shutdown_handler(server);
  CF_EXPECT(server.AddHandler(&shutdown_handler));

  CvdCommandHandler command_handler(server);
  CF_EXPECT(server.AddHandler(&command_handler));

  server.ServerLoop(server_fd);
  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  auto res = cuttlefish::CvdServerMain(argc, argv);
  CHECK(res.ok()) << "cvd server failed: " << res.error().message();
  return *res;
}

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
#include "host/commands/cvd/server.h"

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/server_client.h"

namespace cuttlefish {

namespace {

class TryAcloudCommand : public CvdServerHandler {
 public:
  INJECT(TryAcloudCommand()) = default;
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return ParseInvocation(request.Message()).command == "try-acloud";
  }
  Result<cvd::Response> Handle(const RequestWithStdio&) override {
    return CF_ERR("TODO(schuffelen)");
  }
  Result<void> Interrupt() override { return CF_ERR("Can't be interrupted."); }
};

std::string BashEscape(const std::string& input) {
  bool safe = true;
  for (const auto& c : input) {
    if ('0' <= c && c <= '9') {
      continue;
    }
    if ('a' <= c && c <= 'z') {
      continue;
    }
    if ('A' <= c && c <= 'Z') {
      continue;
    }
    if (c == '_' || c == '-' || c == '.' || c == ',' || c == '/') {
      continue;
    }
    safe = false;
  }
  using android::base::StringReplace;
  return safe ? input : "'" + StringReplace(input, "'", "\\'", true) + "'";
}

class AcloudCommand : public CvdServerHandler {
 public:
  INJECT(AcloudCommand(CvdCommandHandler& inner_handler,
                       InstanceLockFileManager& lock_file_manager))
      : inner_handler_(inner_handler), lock_file_manager_(lock_file_manager) {}
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    return ParseInvocation(request.Message()).command == "acloud";
  }
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }
    CF_EXPECT(CanHandle(request));

    auto arguments = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(arguments.size() > 0);
    CF_EXPECT(arguments[0] == "create");

    std::stringstream translation_message;
    translation_message << "Translating request `";
    translation_message << "acloud ";
    for (const auto& argument : arguments) {
      translation_message << BashEscape(argument) << " ";
    }
    translation_message.seekp(-1, translation_message.cur);
    translation_message << "`\n";  // Overwrite last space

    arguments.erase(arguments.begin());

    std::vector<Flag> flags;
    bool local_instance_set;
    std::optional<int> local_instance;
    auto local_instance_flag = Flag();
    local_instance_flag.Alias(
        {FlagAliasMode::kFlagConsumesArbitrary, "--local-instance"});
    local_instance_flag.Setter([&local_instance_set,
                                &local_instance](const FlagMatch& m) {
      local_instance_set = true;
      if (m.value != "" && local_instance) {
        LOG(ERROR) << "Instance number already set, was \"" << *local_instance
                   << "\", now set to \"" << m.value << "\"";
        return false;
      } else if (m.value != "" && !local_instance) {
        local_instance = std::stoi(m.value);
      }
      return true;
    });
    flags.emplace_back(local_instance_flag);

    bool verbose = false;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagExact, "-v"})
            .Alias({FlagAliasMode::kFlagExact, "-vv"})
            .Alias({FlagAliasMode::kFlagExact, "--verbose"})
            .Setter([&verbose](const FlagMatch&) { return verbose = true; }));

    CF_EXPECT(ParseFlags(flags, arguments));
    CF_EXPECT(arguments.size() == 0,
              "Unrecognized arguments:'"
                  << android::base::Join(arguments, "', '") << "'");

    CF_EXPECT(local_instance_set == true,
              "Only '--local-instance' is supported");
    std::optional<InstanceLockFile> lock;
    if (local_instance.has_value()) {
      // TODO(schuffelen): Block here if it can be interruptible
      lock = CF_EXPECT(lock_file_manager_.TryAcquireLock(*local_instance));
    } else {
      lock = CF_EXPECT(lock_file_manager_.TryAcquireUnusedLock());
    }
    CF_EXPECT(lock.has_value(), "Could not acquire instance lock");
    CF_EXPECT(CF_EXPECT(lock->Status()) == InUseState::kNotInUse);

    auto dir = TempDir() + "/acloud_cvd_temp/local-instance-" +
               std::to_string(lock->Instance());

    cvd::Request fetch_request;
    auto& fetch_command = *fetch_request.mutable_command_request();
    fetch_command.add_args("cvd");
    fetch_command.add_args("fetch");
    fetch_command.add_args("--directory");
    fetch_command.add_args(dir);
    auto& fetch_env = *fetch_command.mutable_env();
    auto host_artifacts_path =
        request.Message().command_request().env().find("ANDROID_HOST_OUT");
    if (host_artifacts_path ==
        request.Message().command_request().env().end()) {
      cvd::Response response;
      response.mutable_status()->set_code(cvd::Status::FAILED_PRECONDITION);
      response.mutable_status()->set_message(
          "Missing ANDROID_HOST_OUT in client environment.");
      return response;
    }
    fetch_env["ANDROID_HOST_OUT"] = host_artifacts_path->second;

    cvd::Request start_request;
    auto& start_command = *start_request.mutable_command_request();
    start_command.add_args("cvd");
    start_command.add_args("start");
    start_command.add_args("--assembly_dir");
    start_command.add_args(dir + "/cuttlefish_assembly");
    start_command.add_args("--daemon");
    start_command.add_args("--undefok");
    start_command.add_args("report_anonymous_usage_stats");
    start_command.add_args("--report_anonymous_usage_stats");
    start_command.add_args("y");
    auto& start_env = *start_command.mutable_env();
    start_env["ANDROID_HOST_OUT"] = dir;
    start_env["ANDROID_PRODUCT_OUT"] = dir;
    start_env["CUTTLEFISH_INSTANCE"] = std::to_string(lock->Instance());
    start_env["HOME"] = dir;

    auto translation = translation_message.str();
    CF_EXPECT(WriteAll(request.Err(), translation) == translation.size(),
              request.Err()->StrError());

    std::vector<cvd::Request> inner_requests = {fetch_request, start_request};
    for (const auto& inner_proto : inner_requests) {
      std::stringstream effective_command;
      effective_command << "Executing `";
      for (const auto& env_var : inner_proto.command_request().env()) {
        effective_command << BashEscape(env_var.first) << "="
                          << BashEscape(env_var.second) << " ";
      }
      for (const auto& argument : inner_proto.command_request().args()) {
        effective_command << BashEscape(argument) << " ";
      }
      effective_command.seekp(-1, effective_command.cur);
      effective_command << "`\n";  // Overwrite last space
      auto command_str = effective_command.str();

      std::vector<SharedFD> fds;
      if (verbose) {
        fds = request.FileDescriptors();
      } else {
        auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
        CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
        fds = {dev_null, dev_null, dev_null};
      }
      RequestWithStdio inner_request(inner_proto, fds, request.Credentials());

      CF_EXPECT(WriteAll(request.Err(), command_str) == command_str.size(),
                request.Err()->StrError());

      interrupt_lock.unlock();
      auto response = CF_EXPECT(inner_handler_.Handle(inner_request));
      interrupt_lock.lock();
      if (interrupted_) {
        return CF_ERR("Interrupted");
      }
      CF_EXPECT(response.status().code() == cvd::Status::OK,
                "Reason: \"" << response.status().message() << "\"");
      static const char kDoneMsg[] = "Done\n";

      CF_EXPECT(WriteAll(request.Err(), kDoneMsg) == sizeof(kDoneMsg) - 1,
                request.Err()->StrError());
    }

    CF_EXPECT(lock->Status(InUseState::kInUse));

    cvd::Response response;
    response.mutable_command_response();
    return response;
  }
  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(inner_handler_.Interrupt());
    return {};
  }

 private:
  CvdCommandHandler& inner_handler_;
  InstanceLockFileManager& lock_file_manager_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

}  // namespace

fruit::Component<fruit::Required<CvdCommandHandler>> AcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, AcloudCommand>()
      .addMultibinding<CvdServerHandler, TryAcloudCommand>();
}

}  // namespace cuttlefish

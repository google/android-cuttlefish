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

#include "host/commands/cvd/acloud_command.h"

#include <optional>
#include <vector>

#include <android-base/strings.h>
#include <android-base/parseint.h>

#include "cvd_server.pb.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/types.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

namespace {

struct ConvertedAcloudCreateCommand {
  InstanceLockFile lock;
  std::vector<RequestWithStdio> requests;
};

/**
 * Split a string into arguments based on shell tokenization rules.
 *
 * This behaves like `shlex.split` from python where arguments are separated
 * based on whitespace, but quoting and quote escaping is respected. This
 * function effectively removes one level of quoting from its inputs while
 * making the split.
 */
Result<std::vector<std::string>> BashTokenize(const std::string& str) {
  Command command("bash");
  command.AddParameter("-c");
  command.AddParameter("printf '%s\n' ", str);
  std::string stdout_str;
  std::string stderr_str;
  auto ret = RunWithManagedStdio(std::move(command), nullptr, &stdout_str,
                                 &stderr_str);
  CF_EXPECT(ret == 0,
            "printf fail \"" << stdout_str << "\", \"" << stderr_str << "\"");
  return android::base::Split(stdout_str, "\n");
}

class ConvertAcloudCreateCommand {
 public:
  INJECT(ConvertAcloudCreateCommand(InstanceLockFileManager& lock_file_manager))
      : lock_file_manager_(lock_file_manager) {}

  Result<ConvertedAcloudCreateCommand> Convert(
      const RequestWithStdio& request) {
    auto arguments = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(arguments.size() > 0);
    CF_EXPECT(arguments[0] == "create");
    arguments.erase(arguments.begin());

    const auto& request_command = request.Message().command_request();

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

    std::optional<std::string> flavor;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--config"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--flavor"})
            .Setter([&flavor](const FlagMatch& m) {
              flavor = m.value;
              return true;
            }));

    bool verbose = false;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagExact, "-v"})
                           .Alias({FlagAliasMode::kFlagExact, "-vv"})
                           .Alias({FlagAliasMode::kFlagExact, "--verbose"})
                           .Setter([&verbose](const FlagMatch&) {
                             verbose = true;
                             return true;
                           }));

    std::optional<std::string> branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--branch"})
            .Setter([&branch](const FlagMatch& m) {
              branch = m.value;
              return true;
            }));

    bool local_image;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesArbitrary, "--local-image"})
            .Setter([&local_image](const FlagMatch& m) {
              local_image = true;
              return m.value == "";
            }));

    std::optional<std::string> build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build-id"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build_id"})
            .Setter([&build_id](const FlagMatch& m) {
              build_id = m.value;
              return true;
            }));

    std::optional<std::string> build_target;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build-target"})
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--build_target"})
            .Setter([&build_target](const FlagMatch& m) {
              build_target = m.value;
              return true;
            }));

    std::optional<std::string> launch_args;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--launch-args"})
            .Setter([&launch_args](const FlagMatch& m) {
              launch_args = m.value;
              return true;
            }));

    std::optional<std::string> system_branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--system-branch"})
            .Setter([&system_branch](const FlagMatch& m) {
              system_branch = m.value;
              return true;
            }));

    std::optional<std::string> system_build_target;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--system-build-target"})
                           .Setter([&system_build_target](const FlagMatch& m) {
                             system_build_target = m.value;
                             return true;
                           }));

    std::optional<std::string> system_build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--system-build-id"})
            .Setter([&system_build_id](const FlagMatch& m) {
              system_build_id = m.value;
              return true;
            }));

    std::optional<std::string> kernel_branch;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--kernel-branch"})
            .Setter([&kernel_branch](const FlagMatch& m) {
              kernel_branch = m.value;
              return true;
            }));

    std::optional<std::string> kernel_build_target;
    flags.emplace_back(Flag()
                           .Alias({FlagAliasMode::kFlagConsumesFollowing,
                                   "--kernel-build-target"})
                           .Setter([&kernel_build_target](const FlagMatch& m) {
                             kernel_build_target = m.value;
                             return true;
                           }));

    std::optional<std::string> kernel_build_id;
    flags.emplace_back(
        Flag()
            .Alias({FlagAliasMode::kFlagConsumesFollowing, "--kernel-build-id"})
            .Setter([&kernel_build_id](const FlagMatch& m) {
              kernel_build_id = m.value;
              return true;
            }));

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

    static constexpr char kAndroidHostOut[] = "ANDROID_HOST_OUT";

    auto host_artifacts_path = request_command.env().find(kAndroidHostOut);
    CF_EXPECT(host_artifacts_path != request_command.env().end(),
              "Missing " << kAndroidHostOut);

    std::vector<cvd::Request> request_protos;
    if (local_image) {
      CF_EXPECT(!(system_branch || system_build_target || system_build_id),
                "--local-image incompatible with --system-* flags");
      cvd::Request& mkdir_request = request_protos.emplace_back();
      auto& mkdir_command = *mkdir_request.mutable_command_request();
      mkdir_command.add_args("cvd");
      mkdir_command.add_args("mkdir");
      mkdir_command.add_args("-p");
      mkdir_command.add_args(dir);
      auto& mkdir_env = *mkdir_command.mutable_env();
      mkdir_env[kAndroidHostOut] = host_artifacts_path->second;

      cvd::Request& ln_request = request_protos.emplace_back();
      auto& ln_command = *ln_request.mutable_command_request();
      ln_command.add_args("cvd");
      ln_command.add_args("ln");
      ln_command.add_args("-f");
      ln_command.add_args("-s");
      ln_command.add_args(host_artifacts_path->second);
      ln_command.add_args(dir + "/host_bins");
      auto& ln_env = *ln_command.mutable_env();
      ln_env[kAndroidHostOut] = host_artifacts_path->second;
    } else {
      cvd::Request& fetch_request = request_protos.emplace_back();
      auto& fetch_command = *fetch_request.mutable_command_request();
      fetch_command.add_args("cvd");
      fetch_command.add_args("fetch");
      fetch_command.add_args("--directory");
      fetch_command.add_args(dir);
      if (branch || build_id || build_target) {
        fetch_command.add_args("--default_build");
        auto target = build_target ? "/" + *build_target : "";
        auto build = build_id.value_or(branch.value_or("aosp-master"));
        fetch_command.add_args(build + target);
      }
      if (system_branch || system_build_id || system_build_target) {
        fetch_command.add_args("--system_build");
        auto target = system_build_target.value_or(build_target.value_or(""));
        if (target != "") {
          target = "/" + target;
        }
        auto build =
            system_build_id.value_or(system_branch.value_or("aosp-master"));
        fetch_command.add_args(build + target);
      }
      if (kernel_branch || kernel_build_id || kernel_build_target) {
        fetch_command.add_args("--kernel_build");
        auto target = kernel_build_target.value_or("kernel_virt_x86_64");
        auto build = kernel_build_id.value_or(
            branch.value_or("aosp_kernel-common-android-mainline"));
        fetch_command.add_args(build + "/" + target);
      }
      auto& fetch_env = *fetch_command.mutable_env();
      fetch_env[kAndroidHostOut] = host_artifacts_path->second;

      cvd::Request& ln_request = request_protos.emplace_back();
      auto& ln_command = *ln_request.mutable_command_request();
      ln_command.add_args("cvd");
      ln_command.add_args("ln");
      ln_command.add_args("-f");
      ln_command.add_args("-s");
      ln_command.add_args(dir);
      ln_command.add_args(dir + "/host_bins");
      auto& ln_env = *ln_command.mutable_env();
      ln_env[kAndroidHostOut] = host_artifacts_path->second;
    }

    cvd::Request& start_request = request_protos.emplace_back();
    auto& start_command = *start_request.mutable_command_request();
    start_command.add_args("cvd");
    start_command.add_args("start");
    start_command.add_args("--daemon");
    start_command.add_args("--undefok");
    start_command.add_args("report_anonymous_usage_stats");
    start_command.add_args("--report_anonymous_usage_stats");
    start_command.add_args("y");
    if (flavor) {
      start_command.add_args("-config");
      start_command.add_args(flavor.value());
    }
    if (launch_args) {
      for (const auto& arg : CF_EXPECT(BashTokenize(*launch_args))) {
        start_command.add_args(arg);
      }
    }
    static constexpr char kAndroidProductOut[] = "ANDROID_PRODUCT_OUT";
    auto& start_env = *start_command.mutable_env();
    if (local_image) {
      start_env[kAndroidHostOut] = host_artifacts_path->second;

      auto product_out = request_command.env().find(kAndroidProductOut);
      CF_EXPECT(product_out != request_command.env().end(),
                "Missing " << kAndroidProductOut);
      start_env[kAndroidProductOut] = product_out->second;
    } else {
      start_env[kAndroidHostOut] = dir;
      start_env[kAndroidProductOut] = dir;
    }
    start_env[kCuttlefishInstanceEnvVarName] = std::to_string(lock->Instance());
    start_env["HOME"] = dir;
    *start_command.mutable_working_directory() = dir;

    std::vector<SharedFD> fds;
    if (verbose) {
      fds = request.FileDescriptors();
    } else {
      auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
      CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
      fds = {dev_null, dev_null, dev_null};
    }

    ConvertedAcloudCreateCommand ret = {
        .lock = {std::move(*lock)},
    };
    for (auto& request_proto : request_protos) {
      ret.requests.emplace_back(request.Client(), request_proto, fds,
                                request.Credentials());
    }
    return ret;
  }

 private:
  InstanceLockFileManager& lock_file_manager_;
};

static bool IsSubOperationSupported(const RequestWithStdio& request) {
  auto invocation = ParseInvocation(request.Message());
  if (invocation.arguments.empty()) {
    return false;
  }
  return invocation.arguments[0] == "create";
}

class TryAcloudCommand : public CvdServerHandler {
 public:
  INJECT(TryAcloudCommand(ConvertAcloudCreateCommand& converter))
      : converter_(converter) {}
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "try-acloud";
  }

  cvd_common::Args CmdList() const override { return {"try-acloud"}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(IsSubOperationSupported(request));
    CF_EXPECT(converter_.Convert(request));
    return CF_ERR("Unreleased");
  }
  Result<void> Interrupt() override { return CF_ERR("Can't be interrupted."); }

 private:
  ConvertAcloudCreateCommand& converter_;
};

class AcloudCommand : public CvdServerHandler {
 public:
  INJECT(AcloudCommand(CommandSequenceExecutor& executor,
                       ConvertAcloudCreateCommand& converter))
      : executor_(executor), converter_(converter) {}
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "acloud";
  }

  cvd_common::Args CmdList() const override { return {"acloud"}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(IsSubOperationSupported(request));
    auto converted = CF_EXPECT(converter_.Convert(request));
    interrupt_lock.unlock();
    CF_EXPECT(executor_.Execute(converted.requests, request.Err()));

    CF_EXPECT(converted.lock.Status(InUseState::kInUse));

    cvd::Response response;
    response.mutable_command_response();
    return response;
  }
  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

 private:
  CommandSequenceExecutor& executor_;
  ConvertAcloudCreateCommand& converter_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

}  // namespace

fruit::Component<fruit::Required<CommandSequenceExecutor>>
AcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, AcloudCommand>()
      .addMultibinding<CvdServerHandler, TryAcloudCommand>();
}

}  // namespace cuttlefish

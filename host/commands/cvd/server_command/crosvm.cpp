/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/server_command/crosvm.h"

#include <android-base/strings.h>

#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdCrosVmCommandHandler : public CvdServerHandler {
 public:
  INJECT(CvdCrosVmCommandHandler(InstanceManager& instance_manager,
                                 SubprocessWaiter& subprocess_waiter))
      : instance_manager_{instance_manager},
        subprocess_waiter_(subprocess_waiter),
        crosvm_operations_{"suspend", "resume", "snapshot"} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const {
    auto invocation = ParseInvocation(request.Message());
    return Contains(crosvm_operations_, invocation.command);
  }

  Result<selector::LocalInstance::Copy> NarrowDownToInstance(
      const selector::LocalInstanceGroup& group,
      cvd_common::Args selector_opts) const {
    auto& instances = group.Instances();
    CF_EXPECT(!instances.empty(),
              "The group " << group.GroupName() << " does not have instance.");
    if (group.Instances().size() == 1) {
      auto& instance_uniq_ptr = *(instances.begin());
      CF_EXPECT(instance_uniq_ptr != nullptr);
      return instance_uniq_ptr->GetCopy();
    }

    auto selector_flags = selector::SelectorFlags::New();
    auto instance_name_flag =
        CF_EXPECT(selector_flags.GetFlag("instance_name"));
    std::optional<std::string> instance_name_flag_value;
    CF_EXPECT(
        instance_name_flag.FilterFlag(selector_opts, instance_name_flag_value));
    CF_EXPECT(
        instance_name_flag_value != std::nullopt,
        "Cannot pin point the instance with in the \"" << group.GroupName());

    auto name_tokens = android::base::Tokenize(*instance_name_flag_value, ",");
    CF_EXPECT_EQ(name_tokens.size(), 1,
                 "Too many or little instance names are given.");
    for (const auto& instance : instances) {
      if (instance && instance->PerInstanceName() == name_tokens.front()) {
        return instance->GetCopy();
      }
    }
    return CF_ERR("Instance named "
                  << name_tokens.front() << " is not found in "
                  << " the group named " << group.GroupName());
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    // TODO(kwstephenkim): implement "--help"
    std::unique_lock interrupt_lock(interruptible_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(VerifyPrecondition(request));
    const uid_t uid = request.Credentials()->uid;

    cvd_common::Envs envs =
        cvd_common::ConvertToEnvs(request.Message().command_request().env());
    const auto& selector_opts =
        request.Message().command_request().selector_opts();
    const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());

    auto instance_group =
        CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs, uid));
    // Unfortunately, for now, cvd selector always returns the instance group
    // that the individual device might belong to
    const auto instance =
        CF_EXPECT(NarrowDownToInstance(instance_group, selector_args));
    const auto instance_id = instance.InstanceId();
    auto home = instance_group.HomeDir();
    const auto socket_file_path =
        ConcatToString(home, "/cuttlefish_runtime.", instance_id,
                       "/internal/"
                       "crosvm_control.sock");

    auto android_host_out = instance_group.HostArtifactsPath();
    auto crosvm_bin_path = ConcatToString(android_host_out, "/bin/crosvm");

    auto [crosvm_op, subcmd_args] = ParseInvocation(request.Message());

    std::stringstream crosvm_cmd;
    crosvm_cmd << crosvm_bin_path << " " << crosvm_op << " ";
    for (const auto& arg : subcmd_args) {
      crosvm_cmd << arg << " ";
    }
    crosvm_cmd << socket_file_path << std::endl;
    WriteAll(request.Err(), crosvm_cmd.str());

    cvd_common::Args crosvm_args{crosvm_op};
    crosvm_args.insert(crosvm_args.end(), subcmd_args.begin(),
                       subcmd_args.end());
    crosvm_args.push_back(socket_file_path);
    envs["HOME"] = home;
    envs[kAndroidHostOut] = android_host_out;
    envs[kAndroidSoongHostOut] = android_host_out;
    ConstructCommandParam construct_cmd_param{
        .bin_path = crosvm_bin_path,
        .home = home,
        .args = crosvm_args,
        .envs = envs,
        .working_dir = request.Message().command_request().working_directory(),
        .command_name = "crosvm",
        .in = request.In(),
        .out = request.Out(),
        .err = request.Err()};
    Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));
    SubprocessOptions options;
    CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));
    interrupt_lock.unlock();

    auto infop = CF_EXPECT(subprocess_waiter_.Wait());
    return ResponseFromSiginfo(infop);
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interruptible_);
    interrupted_ = true;
    CF_EXPECT(subprocess_waiter_.Interrupt());
    return {};
  }
  cvd_common::Args CmdList() const override {
    return cvd_common::Args(crosvm_operations_.begin(),
                            crosvm_operations_.end());
  }

 private:
  Result<void> VerifyPrecondition(const RequestWithStdio& request) const {
    auto verification = cuttlefish::VerifyPrecondition(request);
    CF_EXPECT(verification.is_ok == true, verification.error_message);
    return {};
  }
  InstanceManager& instance_manager_;
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> crosvm_operations_;
};

fruit::Component<fruit::Required<InstanceManager, SubprocessWaiter>>
CvdCrosVmComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, CvdCrosVmCommandHandler>();
}

}  // namespace cuttlefish

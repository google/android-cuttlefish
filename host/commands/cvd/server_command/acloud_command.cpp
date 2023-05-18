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

#include "host/commands/cvd/server_command/acloud_command.h"

#include <atomic>
#include <mutex>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/server_command/acloud_common.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class AcloudCommand : public CvdServerHandler {
 public:
  INJECT(AcloudCommand(CommandSequenceExecutor& executor))
      : executor_(executor) {}
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    if (invocation.arguments.size() >= 2) {
      if (invocation.command == "acloud" &&
          (invocation.arguments[0] == "translator" ||
           invocation.arguments[0] == "mix-super-image")) {
        return false;
      }
    }
    return invocation.command == "acloud";
  }

  cvd_common::Args CmdList() const override { return {"acloud"}; }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CanHandle(request));
    CF_EXPECT(IsSubOperationSupported(request));
    // ConvertAcloudCreate may lock and unlock the lock
    auto converted = CF_EXPECT(
        acloud_impl::ConvertAcloudCreate(request, waiter_, interrupt_lock));
    if (!converted.interrupt_lock_released) {
      interrupt_lock.unlock();
    }
    CF_EXPECT(executor_.Execute(converted.prep_requests, request.Err()));
    auto start_response =
        CF_EXPECT(executor_.ExecuteOne(converted.start_request, request.Err()));

    if (!converted.fetch_command_str.empty()) {
      // has cvd fetch command, update the fetch cvd command file
      using android::base::WriteStringToFile;
      CF_EXPECT(WriteStringToFile(converted.fetch_command_str,
                                  converted.fetch_cvd_args_file),
                true);
    }

    auto handle_response_result = HandleStartResponse(start_response);
    if (handle_response_result.ok()) {
      // print
      std::optional<SharedFD> fd_opt;
      if (converted.verbose) {
        fd_opt = request.Err();
      }
      auto write_result = PrintBriefSummary(*handle_response_result, fd_opt);
      if (!write_result.ok()) {
        LOG(ERROR) << "Failed to write the start response report.";
      }
    } else {
      LOG(ERROR) << "Failed to analyze the cvd start response.";
    }
    cvd::Response response;
    response.mutable_command_response();
    return response;
  }
  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(waiter_.Interrupt());
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

 private:
  Result<cvd::InstanceGroupInfo> HandleStartResponse(
      const cvd::Response& start_response);
  Result<void> PrintBriefSummary(const cvd::InstanceGroupInfo& group_info,
                                 std::optional<SharedFD> stream_fd) const;

  CommandSequenceExecutor& executor_;
  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
  SubprocessWaiter waiter_;
};

Result<cvd::InstanceGroupInfo> AcloudCommand::HandleStartResponse(
    const cvd::Response& start_response) {
  CF_EXPECT(start_response.has_command_response(),
            "cvd start did ont return a command response.");
  const auto& start_command_response = start_response.command_response();
  CF_EXPECT(start_command_response.has_instance_group_info(),
            "cvd start command response did not return instance_group_info.");
  cvd::InstanceGroupInfo group_info =
      start_command_response.instance_group_info();
  return group_info;
}

Result<void> AcloudCommand::PrintBriefSummary(
    const cvd::InstanceGroupInfo& group_info,
    std::optional<SharedFD> stream_fd) const {
  if (!stream_fd) {
    return {};
  }
  SharedFD fd = *stream_fd;
  std::stringstream ss;
  const std::string& group_name = group_info.group_name();
  CF_EXPECT_EQ(group_info.home_directories().size(), 1);
  const std::string home_dir = (group_info.home_directories())[0];
  std::vector<std::string> instance_names;
  std::vector<unsigned> instance_ids;
  instance_names.reserve(group_info.instances().size());
  instance_ids.reserve(group_info.instances().size());
  for (const auto& instance : group_info.instances()) {
    instance_names.push_back(instance.name());
    instance_ids.push_back(instance.instance_id());
  }
  ss << std::endl << "Created instance group: " << group_name << std::endl;
  for (size_t i = 0; i != instance_ids.size(); i++) {
    std::string device_name = group_name + "-" + instance_names[i];
    ss << "  " << device_name << " (local-instance-" << instance_ids[i] << ")"
       << std::endl;
  }
  ss << std::endl
     << "acloud list or cvd fleet for more information." << std::endl;
  auto n_write = WriteAll(*stream_fd, ss.str());
  CF_EXPECT_EQ(n_write, ss.str().size());
  return {};
}

fruit::Component<fruit::Required<CommandSequenceExecutor>>
AcloudCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, AcloudCommand>();
}

}  // namespace cuttlefish

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
#include "host/commands/cvd/demo_multi_vd.h"

#include <chrono>
#include <mutex>
#include <string>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/server.h"
#include "server_client.h"

namespace cuttlefish {

struct DemoCommandSequence {
  std::vector<InstanceLockFile> instance_locks;
  std::vector<RequestWithStdio> requests;
};

class DemoMultiVdCommand : public CvdServerHandler {
 public:
  INJECT(DemoMultiVdCommand(CommandSequenceExecutor& executor,
                            InstanceLockFileManager& lock_file_manager))
      : executor_(executor), lock_file_manager_(lock_file_manager) {}
  ~DemoMultiVdCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "experimental" &&
           invocation.arguments.size() >= 1 &&
           invocation.arguments[0] == "create_phone_tablet";
  }
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }
    CF_EXPECT(CF_EXPECT(CanHandle(request)));

    auto commands = CF_EXPECT(CreateCommandSequence(request));
    interrupt_lock.unlock();
    CF_EXPECT(executor_.Execute(commands.requests, request.Err()));

    for (auto& lock : commands.instance_locks) {
      CF_EXPECT(lock.Status(InUseState::kInUse));
    }

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

  Result<DemoCommandSequence> CreateCommandSequence(
      const RequestWithStdio& request) {
    const auto& client_env = request.Message().command_request().env();

    bool help = false;
    bool verbose = false;
    std::string credentials;

    std::vector<Flag> flags;
    flags.emplace_back(GflagsCompatFlag("help", help));
    flags.emplace_back(GflagsCompatFlag("credentials", credentials));
    flags.emplace_back(GflagsCompatFlag("verbose", verbose));
    auto args = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(ParseFlags(flags, args));

    if (help) {
      static constexpr char kHelp[] =
          "Usage: cvd experimental "
          "create_phone_tablet [--verbose] --credentials=XYZ";
      CF_EXPECT(WriteAll(request.Out(), kHelp, sizeof(kHelp)) == sizeof(kHelp));
      return {};
    }

    DemoCommandSequence ret;
    for (int i = 0; i < 2; i++) {
      auto lock = CF_EXPECT(lock_file_manager_.TryAcquireUnusedLock());
      CF_EXPECT(lock.has_value(), "Failed to acquire instance number");
      ret.instance_locks.emplace_back(std::move(*lock));
    }
    std::vector<cvd::Request> req_protos;

    std::string parent_dir = "/tmp/cvd/";
    auto time = std::chrono::system_clock::now().time_since_epoch().count();
    auto phone_home = parent_dir + "/cvd_phone_" + std::to_string(time) + "/";

    if (!DirectoryExists(parent_dir)) {
      auto& mkdir_parent = *req_protos.emplace_back().mutable_command_request();
      *mkdir_parent.mutable_env() = client_env;
      mkdir_parent.add_args("cvd");
      mkdir_parent.add_args("mkdir");
      mkdir_parent.add_args(parent_dir);
    }

    auto& mkdir_phone = *req_protos.emplace_back().mutable_command_request();
    *mkdir_phone.mutable_env() = client_env;
    mkdir_phone.add_args("cvd");
    mkdir_phone.add_args("mkdir");
    mkdir_phone.add_args(phone_home);

    auto& fetch_phone = *req_protos.emplace_back().mutable_command_request();
    *fetch_phone.mutable_env() = client_env;
    fetch_phone.set_working_directory(phone_home);
    fetch_phone.add_args("cvd");
    fetch_phone.add_args("fetch");
    fetch_phone.add_args("--directory=" + phone_home);
    fetch_phone.add_args("-default_build=git_master/cf_x86_64_phone-userdebug");
    fetch_phone.add_args("-credential_source=" + credentials);

    auto& launch_phone = *req_protos.emplace_back().mutable_command_request();
    *fetch_phone.mutable_env() = client_env;
    launch_phone.set_working_directory(phone_home);
    (*launch_phone.mutable_env())["HOME"] = phone_home;
    (*launch_phone.mutable_env())["ANDROID_HOST_OUT"] = phone_home;
    (*launch_phone.mutable_env())["ANDROID_PRODUCT_OUT"] = phone_home;
    launch_phone.add_args("cvd");
    launch_phone.add_args("start");
    launch_phone.add_args("--daemon");
    launch_phone.add_args("--report_anonymous_usage_stats=y");
    auto phone_instance = std::to_string(ret.instance_locks[0].Instance());
    launch_phone.add_args("--base_instance_num=" + phone_instance);

    auto tablet_home = parent_dir + "/cvd_tablet_" + std::to_string(time) + "/";

    auto& mkdir_tablet = *req_protos.emplace_back().mutable_command_request();
    *mkdir_tablet.mutable_env() = client_env;
    mkdir_tablet.add_args("cvd");
    mkdir_tablet.add_args("mkdir");
    mkdir_tablet.add_args(tablet_home);

    auto& fetch_tablet = *req_protos.emplace_back().mutable_command_request();
    *fetch_tablet.mutable_env() = client_env;
    fetch_tablet.set_working_directory(tablet_home);
    fetch_tablet.add_args("cvd");
    fetch_tablet.add_args("fetch");
    fetch_tablet.add_args("--directory=" + tablet_home);
    fetch_tablet.add_args(
        "-default_build=git_master/cf_x86_64_tablet-userdebug");
    fetch_tablet.add_args("-credential_source=" + credentials);

    auto& launch_tablet = *req_protos.emplace_back().mutable_command_request();
    *launch_tablet.mutable_env() = client_env;
    launch_tablet.set_working_directory(tablet_home);
    (*launch_tablet.mutable_env())["HOME"] = tablet_home;
    (*launch_tablet.mutable_env())["ANDROID_HOST_OUT"] = tablet_home;
    (*launch_tablet.mutable_env())["ANDROID_PRODUCT_OUT"] = tablet_home;
    launch_tablet.add_args("cvd");
    launch_tablet.add_args("start");
    launch_tablet.add_args("--daemon");
    launch_tablet.add_args("--report_anonymous_usage_stats=y");
    auto hwsim_path = phone_home + "cuttlefish_runtime." + phone_instance +
                      "/internal/vhost_user_mac80211";
    launch_tablet.add_args("--vhost_user_mac80211_hwsim=" + hwsim_path);
    launch_tablet.add_args("--rootcanal_attach_mode");
    auto tablet_instance = std::to_string(ret.instance_locks[1].Instance());
    launch_tablet.add_args("--base_instance_num=" + tablet_instance);

    std::vector<SharedFD> fds;
    if (verbose) {
      fds = request.FileDescriptors();
    } else {
      auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
      CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
      fds = {dev_null, dev_null, dev_null};
    }

    for (auto& request_proto : req_protos) {
      ret.requests.emplace_back(request.Client(), request_proto, fds,
                                request.Credentials());
    }

    return ret;
  }

 private:
  CommandSequenceExecutor& executor_;
  InstanceLockFileManager& lock_file_manager_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<fruit::Required<CommandSequenceExecutor>>
DemoMultiVdComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, DemoMultiVdCommand>();
}

}  // namespace cuttlefish

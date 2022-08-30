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
#include "instance_lock.h"
#include "server_client.h"

namespace cuttlefish {

struct DemoCommandSequence {
  std::vector<InstanceLockFile> instance_locks;
  std::vector<RequestWithStdio> requests;
};

static constexpr char kParentDir[] = "/tmp/cvd/";

class SerialLaunchCommand : public CvdServerHandler {
 public:
  INJECT(SerialLaunchCommand(CommandSequenceExecutor& executor,
                             InstanceLockFileManager& lock_file_manager))
      : executor_(executor), lock_file_manager_(lock_file_manager) {}
  ~SerialLaunchCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "experimental" &&
           invocation.arguments.size() >= 1 &&
           invocation.arguments[0] == "serial_launch";
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

    struct Device {
      std::string build;
      std::string home_dir;
      InstanceLockFile ins_lock;
    };

    auto time = std::chrono::system_clock::now().time_since_epoch().count();
    std::vector<Device> devices;
    auto& device_flag = flags.emplace_back();
    device_flag.Alias({FlagAliasMode::kFlagPrefix, "--device="});
    device_flag.Alias({FlagAliasMode::kFlagConsumesFollowing, "--device"});
    device_flag.Setter([this, time, &devices, &request](const FlagMatch& mat) {
      auto lock = lock_file_manager_.TryAcquireUnusedLock();
      if (!lock.ok()) {
        WriteAll(request.Err(), lock.error().message());
        return false;
      } else if (!lock->has_value()) {
        constexpr char kError[] = "could not acquire instance lock";
        WriteAll(request.Err(), kError, sizeof(kError));
        return false;
      }
      int num = (*lock)->Instance();
      std::string home_dir =
          kParentDir + std::to_string(time) + "_" + std::to_string(num) + "/";
      devices.emplace_back(Device{
          .build = mat.value,
          .home_dir = std::move(home_dir),
          .ins_lock = std::move(**lock),
      });
      return true;
    });

    auto args = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(ParseFlags(flags, args));

    if (help) {
      static constexpr char kHelp[] =
          "Usage: cvd experimental serial_launch [--verbose] --credentials=XYZ "
          "--device=build/target --device=build/target";
      CF_EXPECT(WriteAll(request.Out(), kHelp, sizeof(kHelp)) == sizeof(kHelp));
      return {};
    }

    DemoCommandSequence ret;
    for (int i = 0; i < devices.size(); i++) {
      auto lock = CF_EXPECT(lock_file_manager_.TryAcquireUnusedLock());
      CF_EXPECT(lock.has_value(), "Failed to acquire instance number");
      ret.instance_locks.emplace_back(std::move(*lock));
    }
    std::vector<cvd::Request> req_protos;

    if (!DirectoryExists(kParentDir)) {
      auto& mkdir_parent = *req_protos.emplace_back().mutable_command_request();
      *mkdir_parent.mutable_env() = client_env;
      mkdir_parent.add_args("cvd");
      mkdir_parent.add_args("mkdir");
      mkdir_parent.add_args(kParentDir);
    }

    bool is_first = true;

    for (const auto& device : devices) {
      auto& mkdir_cmd = *req_protos.emplace_back().mutable_command_request();
      *mkdir_cmd.mutable_env() = client_env;
      mkdir_cmd.add_args("cvd");
      mkdir_cmd.add_args("mkdir");
      mkdir_cmd.add_args(device.home_dir);

      auto& fetch_cmd = *req_protos.emplace_back().mutable_command_request();
      *fetch_cmd.mutable_env() = client_env;
      fetch_cmd.set_working_directory(device.home_dir);
      fetch_cmd.add_args("cvd");
      fetch_cmd.add_args("fetch");
      fetch_cmd.add_args("--directory=" + device.home_dir);
      fetch_cmd.add_args("-default_build=" + device.build);
      fetch_cmd.add_args("-credential_source=" + credentials);

      auto& launch_cmd = *req_protos.emplace_back().mutable_command_request();
      *launch_cmd.mutable_env() = client_env;
      launch_cmd.set_working_directory(device.home_dir);
      (*launch_cmd.mutable_env())["HOME"] = device.home_dir;
      (*launch_cmd.mutable_env())["ANDROID_HOST_OUT"] = device.home_dir;
      (*launch_cmd.mutable_env())["ANDROID_PRODUCT_OUT"] = device.home_dir;
      launch_cmd.add_args("cvd");
      launch_cmd.add_args("start");
      launch_cmd.add_args("--daemon");
      launch_cmd.add_args("--report_anonymous_usage_stats=y");
      launch_cmd.add_args("--base_instance_num=" +
                          std::to_string(device.ins_lock.Instance()));

      if (is_first) {
        is_first = false;
        continue;
      }
      const auto& first = devices[0];
      auto hwsim_path = first.home_dir + "cuttlefish_runtime." +
                        std::to_string(first.ins_lock.Instance()) +
                        "/internal/vhost_user_mac80211";
      launch_cmd.add_args("--vhost_user_mac80211_hwsim=" + hwsim_path);
      launch_cmd.add_args("--rootcanal_attach_mode");
    }

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

class SerialPreset : public CvdServerHandler {
 public:
  INJECT(SerialPreset(CommandSequenceExecutor& executor))
      : executor_(executor) {}
  ~SerialPreset() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "experimental" &&
           invocation.arguments.size() >= 1 &&
           Presets().count(invocation.arguments[0]) > 0;
  }
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    if (interrupted_) {
      return CF_ERR("Interrupted");
    }
    CF_EXPECT(CF_EXPECT(CanHandle(request)));

    auto invocation = ParseInvocation(request.Message());
    CF_EXPECT(invocation.arguments.size() >= 1);
    const auto& presets = Presets();
    auto devices = presets.find(invocation.arguments[0]);
    CF_EXPECT(devices != presets.end(), "could not find preset");

    cvd::Request inner_req_proto = request.Message();
    auto& cmd = *inner_req_proto.mutable_command_request();
    cmd.clear_args();
    cmd.add_args("cvd");
    cmd.add_args("experimental");
    cmd.add_args("serial_launch");
    for (const auto& device : devices->second) {
      cmd.add_args("--device=" + device);
    }
    for (int i = 1; i < invocation.arguments.size(); i++) {
      cmd.add_args(invocation.arguments[i]);
    }

    RequestWithStdio inner_request(request.Client(), std::move(inner_req_proto),
                                   request.FileDescriptors(),
                                   request.Credentials());

    CF_EXPECT(executor_.Execute({std::move(inner_request)}, request.Err()));
    interrupt_lock.unlock();

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

  static std::unordered_map<std::string, std::vector<std::string>> Presets() {
    return {
        {"create_phone_tablet",
         {"git_master/cf_x86_64_phone-userdebug",
          "git_master/cf_x86_64_tablet-userdebug"}},
        {"create_phone_wear",
         {"git_master/cf_x86_64_phone-userdebug", "git_master/cf_gwear_x86"}},
    };
  }

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<fruit::Required<CommandSequenceExecutor>>
DemoMultiVdComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, SerialLaunchCommand>()
      .addMultibinding<CvdServerHandler, SerialPreset>();
}

}  // namespace cuttlefish

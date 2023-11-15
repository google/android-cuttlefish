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

#include "host/commands/cvd/server_command/serial_launch.h"

#include <sys/types.h>

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "android-base/parseint.h"
#include "android-base/strings.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

// copied from "../demo_multi_vd.cpp"
namespace cuttlefish {
namespace {

template <typename... Args>
cvd::Request CreateCommandRequest(
    const google::protobuf::Map<std::string, std::string>& envs,
    Args&&... args) {
  cvd::Request request;
  auto& cmd_request = *request.mutable_command_request();
  (cmd_request.add_args(std::forward<Args>(args)), ...);
  *cmd_request.mutable_env() = envs;
  return request;
}

std::vector<cvd::Request> AppendRequestVectors(
    std::vector<cvd::Request>&& dest, std::vector<cvd::Request>&& src) {
  auto merged = std::move(dest);
  for (auto& request : src) {
    merged.emplace_back(std::move(request));
  }
  return merged;
}

struct DemoCommandSequence {
  std::vector<InstanceLockFile> instance_locks;
  std::vector<RequestWithStdio> requests;
};

/** Returns a `Flag` object that accepts comma-separated unsigned integers. */
template <typename T>
Flag DeviceSpecificUintFlag(const std::string& name, std::vector<T>& values) {
  return GflagsCompatFlag(name).Setter(
      [&values](const FlagMatch& match) -> Result<void> {
        auto parsed_values = android::base::Tokenize(match.value, ", ");
        for (auto& parsed_value : parsed_values) {
          std::uint32_t num = 0;
          CF_EXPECTF(android::base::ParseUint(parsed_value, &num),
                     "Failed to parse {} as an integer", parsed_value);
          values.push_back(num);
        }
        return {};
      });
}

/** Returns a `Flag` object that accepts comma-separated strings. */
Flag DeviceSpecificStringFlag(const std::string& name,
                              std::vector<std::string>& values) {
  return GflagsCompatFlag(name).Setter(
      [&values](const FlagMatch& match) -> Result<void> {
        auto parsed_values = android::base::Tokenize(match.value, ", ");
        for (auto& parsed_value : parsed_values) {
          values.push_back(parsed_value);
        }
        return {};
      });
}

std::string ParentDir(const uid_t uid) {
  constexpr char kParentDirPrefix[] = "/tmp/cvd/";
  std::stringstream ss;
  ss << kParentDirPrefix << uid << "/";
  return ss.str();
}

}  // namespace

class SerialLaunchCommand : public CvdServerHandler {
 public:
  SerialLaunchCommand(CommandSequenceExecutor& executor,
                      InstanceLockFileManager& lock_file_manager)
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

  cvd_common::Args CmdList() const override { return {"experimental"}; }

  Result<DemoCommandSequence> CreateCommandSequence(
      const RequestWithStdio& request) {
    const auto& client_env = request.Message().command_request().env();
    const auto client_uid = CF_EXPECT(request.Credentials()).uid;

    std::vector<Flag> flags;

    bool help = false;
    flags.emplace_back(GflagsCompatFlag("help", help));

    std::string credentials;
    flags.emplace_back(GflagsCompatFlag("credentials", credentials));

    bool verbose = false;
    flags.emplace_back(GflagsCompatFlag("verbose", verbose));

    std::vector<std::uint32_t> x_res;
    flags.emplace_back(DeviceSpecificUintFlag("x_res", x_res));

    std::vector<std::uint32_t> y_res;
    flags.emplace_back(DeviceSpecificUintFlag("y_res", y_res));

    std::vector<std::uint32_t> dpi;
    flags.emplace_back(DeviceSpecificUintFlag("dpi", dpi));

    std::vector<std::uint32_t> cpus;
    flags.emplace_back(DeviceSpecificUintFlag("cpus", cpus));

    std::vector<std::uint32_t> memory_mb;
    flags.emplace_back(DeviceSpecificUintFlag("memory_mb", memory_mb));

    std::vector<std::string> setupwizard_mode;
    flags.emplace_back(
        DeviceSpecificStringFlag("setupwizard_mode", setupwizard_mode));

    std::vector<std::string> report_anonymous_usage_stats;
    flags.emplace_back(DeviceSpecificStringFlag("report_anonymous_usage_stats",
                                                report_anonymous_usage_stats));

    std::vector<std::string> webrtc_device_id;
    flags.emplace_back(
        DeviceSpecificStringFlag("webrtc_device_id", webrtc_device_id));

    bool daemon = true;
    flags.emplace_back(GflagsCompatFlag("daemon", daemon));

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
    device_flag.Setter([this, time, client_uid,
                        &devices](const FlagMatch& mat) -> Result<void> {
      auto lock = CF_EXPECT(lock_file_manager_.TryAcquireUnusedLock());
      CF_EXPECT(lock.has_value(), "could not acquire instance lock");
      int num = lock->Instance();
      std::string home_dir = ParentDir(client_uid) + std::to_string(time) +
                             "_" + std::to_string(num) + "/";
      devices.emplace_back(Device{
          .build = mat.value,
          .home_dir = std::move(home_dir),
          .ins_lock = std::move(*lock),
      });
      return {};
    });

    auto args = ParseInvocation(request.Message()).arguments;
    for (const auto& arg : args) {
      std::string message = "argument: \"" + arg + "\"\n";
      CF_EXPECT(WriteAll(request.Err(), message) == message.size());
    }

    CF_EXPECT(ParseFlags(flags, args));

    if (help) {
      static constexpr char kHelp[] =
          "Usage: cvd experimental serial_launch [--verbose] --credentials=XYZ "
          "--device=build/target --device=build/target";
      CF_EXPECT(WriteAll(request.Out(), kHelp, sizeof(kHelp)) == sizeof(kHelp));
      return {};
    }

    CF_EXPECT(devices.size() < 2 || daemon,
              "--daemon=true required for more than 1 device");

    std::vector<std::vector<std::uint32_t>*> int_device_args = {
        &x_res, &y_res, &dpi, &cpus, &memory_mb,
    };
    for (const auto& int_device_arg : int_device_args) {
      CF_EXPECT(int_device_arg->size() == 0 ||
                    int_device_arg->size() == devices.size(),
                "If given, device-specific flags should have as many values as "
                "there are `--device` arguments");
    }
    std::vector<std::vector<std::string>*> string_device_args = {
        &setupwizard_mode,
        &report_anonymous_usage_stats,
        &webrtc_device_id,
    };
    for (const auto& string_device_arg : string_device_args) {
      CF_EXPECT(string_device_arg->size() == 0 ||
                    string_device_arg->size() == devices.size(),
                "If given, device-specific flags should have as many values as "
                "there are `--device` arguments");
    }

    std::vector<cvd::Request> req_protos;

    auto mkdir_ancestors_requests =
        CF_EXPECT(CreateMkdirCommandRequestRecursively(client_env,
                                                       ParentDir(client_uid)));
    req_protos = AppendRequestVectors(std::move(req_protos),
                                      std::move(mkdir_ancestors_requests));

    bool is_first = true;

    int index = 0;
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
      /* TODO(kwstephenkim): remove kAcquireFileLockOpt flag when
       * SerialLaunchCommand is re-implemented so that it does not have to
       * acquire a file lock.
       */
      launch_cmd.mutable_selector_opts()->add_args(
          std::string("--") + selector::SelectorFlags::kAcquireFileLock +
          "=false");
      launch_cmd.add_args("start");
      launch_cmd.add_args(
          "--undefok=daemon,base_instance_num,x_res,y_res,dpi,cpus,memory_mb,"
          "setupwizard_mode,report_anonymous_usage_stats,webrtc_device_id");
      launch_cmd.add_args("--daemon");
      launch_cmd.add_args("--base_instance_num=" +
                          std::to_string(device.ins_lock.Instance()));
      if (index < x_res.size()) {
        launch_cmd.add_args("--x_res=" + std::to_string(x_res[index]));
      }
      if (index < y_res.size()) {
        launch_cmd.add_args("--y_res=" + std::to_string(y_res[index]));
      }
      if (index < dpi.size()) {
        launch_cmd.add_args("--dpi=" + std::to_string(dpi[index]));
      }
      if (index < cpus.size()) {
        launch_cmd.add_args("--cpus=" + std::to_string(cpus[index]));
      }
      if (index < memory_mb.size()) {
        launch_cmd.add_args("--memory_mb=" + std::to_string(memory_mb[index]));
      }
      if (index < setupwizard_mode.size()) {
        launch_cmd.add_args("--setupwizard_mode=" + setupwizard_mode[index]);
      }
      if (index < report_anonymous_usage_stats.size()) {
        launch_cmd.add_args("--report_anonymous_usage_stats=" +
                            report_anonymous_usage_stats[index]);
      }
      if (index < webrtc_device_id.size()) {
        launch_cmd.add_args("--webrtc_device_id=" + webrtc_device_id[index]);
      }

      index++;
      if (is_first) {
        is_first = false;
        continue;
      }
      const auto& first = devices[0];
      const auto& first_instance_num =
          std::to_string(first.ins_lock.Instance());
      auto hwsim_path = first.home_dir + "cuttlefish_runtime." +
                        first_instance_num + "/internal/vhost_user_mac80211";
      launch_cmd.add_args("--vhost_user_mac80211_hwsim=" + hwsim_path);
      launch_cmd.add_args("--rootcanal_instance_num=" + first_instance_num);
    }

    std::vector<SharedFD> fds;
    if (verbose) {
      fds = request.FileDescriptors();
    } else {
      auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
      CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
      fds = {dev_null, dev_null, dev_null};
    }

    DemoCommandSequence ret;
    for (auto& device : devices) {
      ret.instance_locks.emplace_back(std::move(device.ins_lock));
    }
    for (auto& request_proto : req_protos) {
      ret.requests.emplace_back(request.Client(), request_proto, fds,
                                request.Credentials());
    }

    return ret;
  }

 private:
  Result<std::vector<cvd::Request>> CreateMkdirCommandRequestRecursively(
      const google::protobuf::Map<std::string, std::string>& client_env,
      const std::string& path) {
    std::vector<cvd::Request> output;
    CF_EXPECT(!path.empty() && path.at(0) == '/',
              "Only absolute path is supported.");
    if (path == "/") {
      return output;
    }
    std::string path_exclude_root = path.substr(1);
    std::vector<std::string> tokens =
        android::base::Tokenize(path_exclude_root, "/");
    std::string current_dir = "/";
    for (int i = 0; i < tokens.size(); i++) {
      current_dir.append(tokens[i]);
      if (!DirectoryExists(current_dir)) {
        output.emplace_back(
            CreateCommandRequest(client_env, "cvd", "mkdir", current_dir));
      }
      current_dir.append("/");
    }
    return output;
  }

  CommandSequenceExecutor& executor_;
  InstanceLockFileManager& lock_file_manager_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

std::unique_ptr<CvdServerHandler> NewSerialLaunchCommand(
    CommandSequenceExecutor& executor,
    InstanceLockFileManager& lock_file_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new SerialLaunchCommand(executor, lock_file_manager));
}

}  // namespace cuttlefish

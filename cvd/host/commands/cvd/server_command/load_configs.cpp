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
#include "host/commands/cvd/server_command/load_configs.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <json/json.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/fetch_cvd_parser.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

std::string JoinBySelector(
    const std::vector<FetchCvdInstanceConfig>& collection,
    const std::function<std::string(const FetchCvdInstanceConfig&)>& selector) {
  std::vector<std::string> selected;
  selected.reserve(collection.size());
  for (const auto& instance : collection) {
    selected.emplace_back(selector(instance));
  }
  return android::base::Join(selected, ',');
}

std::optional<std::string> JoinBySelectorOptional(
    const std::vector<FetchCvdInstanceConfig>& collection,
    const std::function<std::string(const FetchCvdInstanceConfig&)>& selector) {
  std::string result = JoinBySelector(collection, selector);
  // no values, empty or only ',' separators
  if (result.size() == collection.size() - 1) {
    return std::nullopt;
  }
  return result;
}

std::string GenerateSystemImageFlag(const FetchCvdConfig& config) {
  auto get_full_path = [&target_directory = config.target_directory](
                           const FetchCvdInstanceConfig& instance_config) {
    return target_directory + "/" + instance_config.target_subdirectory;
  };
  return "--system_image_dir=" +
         JoinBySelector(config.instances, get_full_path);
}

std::string GenerateParentDirectory() {
  const uid_t uid = getuid();
  // Prefix for the parent directory.
  constexpr char kParentDirPrefix[] = "/tmp/cvd/";
  std::stringstream ss;

  // Constructs the full directory path.
  ss << kParentDirPrefix << uid << "/";

  return ss.str();
}

std::string GenerateHostArtifactsDirectory(int64_t time) {
  return GenerateParentDirectory() + std::to_string(time);
}

std::string GenerateHomeDirectoryName(int64_t time) {
  return GenerateParentDirectory() + std::to_string(time) + "_home/";
}

using DemoCommandSequence = std::vector<RequestWithStdio>;

void AddFetchCommandArgs(
    cvd::CommandRequest& command, const FetchCvdConfig& config,
    const std::vector<FetchCvdInstanceConfig>& fetch_instances) {
  command.add_args("cvd");
  command.add_args("fetch");
  command.add_args("--target_directory=" + config.target_directory);
  if (config.credential_source) {
    command.add_args("--credential_source=" + *config.credential_source);
  }

  command.add_args(
      "--target_subdirectory=" +
      JoinBySelector(fetch_instances,
                     [](const FetchCvdInstanceConfig& instance_config) {
                       return instance_config.target_subdirectory;
                     }));
  std::optional<std::string> default_build_params = JoinBySelectorOptional(
      fetch_instances, [](const FetchCvdInstanceConfig& instance_config) {
        return instance_config.default_build.value_or("");
      });
  if (default_build_params) {
    command.add_args("--default_build=" + *default_build_params);
  }
  std::optional<std::string> system_build_params = JoinBySelectorOptional(
      fetch_instances, [](const FetchCvdInstanceConfig& instance_config) {
        return instance_config.system_build.value_or("");
      });
  if (system_build_params) {
    command.add_args("--system_build=" + *system_build_params);
  }
  std::optional<std::string> kernel_build_params = JoinBySelectorOptional(
      fetch_instances, [](const FetchCvdInstanceConfig& instance_config) {
        return instance_config.kernel_build.value_or("");
      });
  if (kernel_build_params) {
    command.add_args("--kernel_build=" + *kernel_build_params);
  }
}

}  // namespace

class LoadConfigsCommand : public CvdServerHandler {
 public:
  INJECT(LoadConfigsCommand(CommandSequenceExecutor& executor))
      : executor_(executor) {}
  ~LoadConfigsCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == kLoadSubCmd;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    CF_EXPECT(CF_EXPECT(CanHandle(request)));

    auto commands = CF_EXPECT(CreateCommandSequence(request));
    interrupt_lock.unlock();
    CF_EXPECT(executor_.Execute(commands, request.Err()));

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

  cvd_common::Args CmdList() const override { return {kLoadSubCmd}; }

  // TODO(moelsherif): expand this enum in the future to support more types ( double , float
  // , etc) if neeeded
  enum ArgValueType { UINTEGER, BOOLEAN, TEXT };

  bool IsUnsignedInteger(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(),
                                       [](char c) { return std::isdigit(c); });
  }

  ArgValueType GetArgValueType(const std::string& str) {
    if (IsUnsignedInteger(str)) {
      return UINTEGER;
    }

    if (str == "true" || str == "false") {
      return BOOLEAN;
    }

    // Otherwise, treat the string as text
    return TEXT;
  }

  Json::Value ConvertArgToJson(const std::string& key,
                               const std::string& leafValue) {
    std::stack<std::string> levels;
    std::stringstream ks(key);
    std::string token;
    while (std::getline(ks, token, '.')) {
      levels.push(token);
    }

    // assign the leaf value based on the type of input value
    Json::Value leaf;
    if (GetArgValueType(leafValue) == UINTEGER) {
      std::uint32_t leaf_val{};
      if (!android::base::ParseUint(leafValue ,&leaf_val)){
        LOG(ERROR) << "Failed to parse unsigned integer " << leafValue;
        return Json::Value::null;
      };
      leaf = leaf_val;
    } else if (GetArgValueType(leafValue) == BOOLEAN) {
      leaf = (leafValue == "true");
    } else {
      leaf = leafValue;
    }

    while (!levels.empty()) {
      Json::Value curr;
      std::string index = levels.top();

      if (GetArgValueType(index) == UINTEGER) {
        std::uint32_t index_val{};
        if (!android::base::ParseUint(index, &index_val)){
          LOG(ERROR) << "Failed to parse unsigned integer " << index;
          return Json::Value::null;
        }
        curr[index_val] = leaf;
      } else {
        curr[index] = leaf;
      }

      leaf = curr;
      levels.pop();
    }

    return leaf;
  }

  Json::Value ParseArgsToJson(const std::vector<std::string>& strings) {
    Json::Value jsonValue;
    for (const auto& str : strings) {
      std::string key;
      std::string value;
      size_t equals_pos = str.find('=');
      if (equals_pos != std::string::npos) {
        key = str.substr(0, equals_pos);
        value = str.substr(equals_pos + 1);
      } else {
        key = str;
        value.clear();
        LOG(WARNING) << "No value provided for key " << key;
        return Json::Value::null;
      }
      MergeTwoJsonObjs(jsonValue, ConvertArgToJson(key, value));
    }

    return jsonValue;
  }

  Result<void> ValidateArgFormat(const std::string& str) {
    auto equalsPos = str.find('=');
    CF_EXPECT(equalsPos != std::string::npos,
              "equal value is not provided in the argument");
    std::string prefix = str.substr(0, equalsPos);
    CF_EXPECT(!prefix.empty(), "argument value should not be empty");
    CF_EXPECT(prefix.find('.') != std::string::npos,
              "argument value must be dot separated");
    CF_EXPECT(prefix[0] != '.', "argument value should not start with a dot");
    CF_EXPECT(prefix.find("..") == std::string::npos,
              "argument value should not contain two consecutive dots");
    CF_EXPECT(prefix.back() != '.', "argument value should not end with a dot");
    return {};
  }

  Result<void> ValidateArgsFormat(const std::vector<std::string>& strings) {
    for (const auto& str : strings) {
      CF_EXPECT(ValidateArgFormat(str),
                "Invalid  argument format. " << str << " Please use arg=value");
    }
    return {};
  }

  Result<DemoCommandSequence> CreateCommandSequence(
      const RequestWithStdio& request) {
    bool help = false;

    std::vector<Flag> flags;
    flags.emplace_back(GflagsCompatFlag("help", help));
    std::vector<std::string> overrides;
    FlagAlias alias = {FlagAliasMode::kFlagPrefix, "--override="};
    flags.emplace_back(
        Flag().Alias(alias).Setter([&overrides](const FlagMatch& m) {
          overrides.push_back(m.value);
          return true;
        }));
    auto args = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(ParseFlags(flags, args));
    CF_EXPECT(args.size() > 0,
              "No arguments provided to cvd load command, please provide at "
              "least one argument (help or path to json file)");

    if (help) {
      std::stringstream help_msg_stream;
      help_msg_stream << "Usage: cvd " << kLoadSubCmd;
      const auto help_msg = help_msg_stream.str();
      CF_EXPECT(WriteAll(request.Out(), help_msg) == help_msg.size());
      return {};
    }

    // Extract the config_path from the arguments
    std::string config_path = args.front();
    Json::Value json_configs =
        CF_EXPECT(ParseJsonFile(config_path), "parsing input file failed");

    // remove the config_path from the arguments
    args.erase(args.begin());

    // Handle the rest of the arguments (Overrides)
    if (overrides.size() > 0) {
      // Validate all arguments follow specific pattern
      CF_EXPECT(ValidateArgsFormat(overrides),
                "override parameters are not in the correct format");

      // Convert all arguments to json tree
      auto args_tree = ParseArgsToJson(overrides);
      MergeTwoJsonObjs(json_configs, args_tree);
    }

    auto cvd_flags =
        CF_EXPECT(ParseCvdConfigs(json_configs), "parsing json configs failed");

    int num_instances = cvd_flags.fetch_cvd_flags.instances.size();
    CF_EXPECT_GT(num_instances, 0, "No instances to load");

    std::vector<cvd::Request> req_protos;

    const auto& client_env = request.Message().command_request().env();

    auto time = std::chrono::system_clock::now().time_since_epoch().count();
    cvd_flags.fetch_cvd_flags.target_directory =
        GenerateHostArtifactsDirectory(time);
    for (int instance_index = 0; instance_index < num_instances;
         instance_index++) {
      LOG(INFO) << "Instance " << instance_index << " directory is "
                << cvd_flags.fetch_cvd_flags.target_directory << "/"
                << std::to_string(instance_index);
      cvd_flags.fetch_cvd_flags.instances[instance_index].target_subdirectory =
          std::to_string(instance_index);
    }

    std::vector<FetchCvdInstanceConfig> fetch_instances;
    for (const auto& instance : cvd_flags.fetch_cvd_flags.instances) {
      if (instance.should_fetch) {
        fetch_instances.emplace_back(instance);
      }
    }
    if (fetch_instances.size() > 0) {
      auto& fetch_cmd = *req_protos.emplace_back().mutable_command_request();
      *fetch_cmd.mutable_env() = client_env;
      AddFetchCommandArgs(fetch_cmd, cvd_flags.fetch_cvd_flags,
                          fetch_instances);
    }

    // Create the launch home directory
    std::string launch_home_dir = GenerateHomeDirectoryName(time);
    auto& mkdir_cmd = *req_protos.emplace_back().mutable_command_request();
    *mkdir_cmd.mutable_env() = client_env;
    mkdir_cmd.add_args("cvd");
    mkdir_cmd.add_args("mkdir");
    mkdir_cmd.add_args("-p");
    mkdir_cmd.add_args(launch_home_dir);

    // Handle the launch command
    auto& launch_cmd = *req_protos.emplace_back().mutable_command_request();

    auto first_instance_dir =
        cvd_flags.fetch_cvd_flags.target_directory + "/" +
        cvd_flags.fetch_cvd_flags.instances[0].target_subdirectory;
    *launch_cmd.mutable_env() = client_env;
    launch_cmd.set_working_directory(first_instance_dir);
    (*launch_cmd.mutable_env())["HOME"] = launch_home_dir;

    (*launch_cmd.mutable_env())[kAndroidHostOut] = first_instance_dir;
    (*launch_cmd.mutable_env())[kAndroidSoongHostOut] = first_instance_dir;

    if (Contains(*launch_cmd.mutable_env(), kAndroidProductOut)) {
      (*launch_cmd.mutable_env()).erase(kAndroidProductOut);
    }

    /* cvd load will always create instances in deamon mode (to be independent
     of terminal) and will enable reporting automatically (to run automatically
     without question during launch)
     */
    launch_cmd.add_args("cvd");
    launch_cmd.add_args("start");
    launch_cmd.add_args("--daemon");
    for (auto& parsed_flag : cvd_flags.launch_cvd_flags) {
      launch_cmd.add_args(parsed_flag);
    }
    // Add system flag for multi-build scenario
    launch_cmd.add_args(GenerateSystemImageFlag(cvd_flags.fetch_cvd_flags));

    launch_cmd.mutable_selector_opts()->add_args(
        std::string("--") + selector::SelectorFlags::kDisableDefaultGroup);

    /*Verbose is disabled by default*/
    auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
    std::vector<SharedFD> fds = {dev_null, dev_null, dev_null};
    DemoCommandSequence ret;

    for (auto& request_proto : req_protos) {
      ret.emplace_back(RequestWithStdio(request.Client(), request_proto, fds,
                                        request.Credentials()));
    }

    return ret;
  }

 private:
  static constexpr char kLoadSubCmd[] = "load";

  CommandSequenceExecutor& executor_;

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
};

fruit::Component<fruit::Required<CommandSequenceExecutor>>
LoadConfigsComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, LoadConfigsCommand>();
}

}  // namespace cuttlefish

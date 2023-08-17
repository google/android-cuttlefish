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

#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <json/json.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/parser/fetch_cvd_parser.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

std::optional<std::string> JoinBySelectorOptional(
    const std::vector<FetchCvdInstanceConfig>& collection,
    const std::function<std::string(const FetchCvdInstanceConfig&)>& selector) {
  std::vector<std::string> selected;
  selected.reserve(collection.size());
  for (const auto& instance : collection) {
    selected.emplace_back(selector(instance));
  }
  std::string result = android::base::Join(selected, ',');
  // no values, empty or only ',' separators
  if (result.size() == collection.size() - 1) {
    return std::nullopt;
  }
  return result;
}

void AddFetchCommandArgs(
    cvd::CommandRequest& command, const FetchCvdConfig& config,
    const std::vector<FetchCvdInstanceConfig>& fetch_instances,
    const LoadDirectories& load_directories) {
  command.add_args("cvd");
  command.add_args("fetch");
  command.add_args("--target_directory=" + load_directories.target_directory);
  if (config.api_key) {
    command.add_args("--api_key=" + *config.api_key);
  }
  if (config.credential_source) {
    command.add_args("--credential_source=" + *config.credential_source);
  }
  if (config.wait_retry_period) {
    command.add_args("--wait_retry_period=" + *config.wait_retry_period);
  }
  if (config.external_dns_resolver) {
    command.add_args("--external_dns_resolver=" +
                     *config.external_dns_resolver);
  }
  if (config.keep_downloaded_archives) {
    command.add_args("--keep_downloaded_archives=" +
                     *config.keep_downloaded_archives);
  }

  command.add_args(
      "--target_subdirectory=" +
      android::base::Join(load_directories.target_subdirectories, ','));
  std::optional<std::string> default_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.default_build.value_or("");
      });
  if (default_build_params) {
    command.add_args("--default_build=" + *default_build_params);
  }
  std::optional<std::string> system_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.system_build.value_or("");
      });
  if (system_build_params) {
    command.add_args("--system_build=" + *system_build_params);
  }
  std::optional<std::string> kernel_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.kernel_build.value_or("");
      });
  if (kernel_build_params) {
    command.add_args("--kernel_build=" + *kernel_build_params);
  }
  std::optional<std::string> boot_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.boot_build.value_or("");
      });
  if (boot_build_params) {
    command.add_args("--boot_build=" + *boot_build_params);
  }
  std::optional<std::string> bootloader_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.bootloader_build.value_or("");
      });
  if (bootloader_build_params) {
    command.add_args("--bootloader_build=" + *bootloader_build_params);
  }
  std::optional<std::string> otatools_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.otatools_build.value_or("");
      });
  if (otatools_build_params) {
    command.add_args("--otatools_build=" + *otatools_build_params);
  }
  std::optional<std::string> host_package_build_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.host_package_build.value_or("");
      });
  if (host_package_build_params) {
    command.add_args("--host_package_build=" + *host_package_build_params);
  }
  std::optional<std::string> download_img_zip_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.download_img_zip.value_or("");
      });
  if (download_img_zip_params) {
    command.add_args("--download_img_zip=" + *download_img_zip_params);
  }
  std::optional<std::string> download_target_files_zip_params =
      JoinBySelectorOptional(fetch_instances, [](const auto& instance_config) {
        return instance_config.download_target_files_zip.value_or("");
      });
  if (download_target_files_zip_params) {
    command.add_args("--download_target_files_zip=" +
                     *download_target_files_zip_params);
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

  Result<std::vector<RequestWithStdio>> CreateCommandSequence(
      const RequestWithStdio& request) {
    bool help = false;

    std::vector<Flag> flags;
    flags.emplace_back(GflagsCompatFlag("help", help));
    std::vector<std::string> overrides;
    FlagAlias alias = {FlagAliasMode::kFlagPrefix, "--override="};
    flags.emplace_back(Flag().Alias(alias).Setter(
        [&overrides](const FlagMatch& m) -> Result<void> {
          overrides.push_back(m.value);
          return {};
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

    std::string config_path = args.front();
    if (config_path[0] != '/') {
      config_path = request.Message().command_request().working_directory() +
                    "/" + config_path;
    }
    Json::Value json_configs =
        CF_EXPECT(GetOverridedJsonConfig(config_path, overrides));
    const auto load_directories =
        CF_EXPECT(GenerateLoadDirectories(json_configs["instances"].size()));
    auto cvd_flags =
        CF_EXPECT(ParseCvdConfigs(json_configs), "parsing json configs failed");
    std::vector<cvd::Request> req_protos;
    const auto& client_env = request.Message().command_request().env();

    std::vector<FetchCvdInstanceConfig> fetch_instances;
    for (const auto& instance : cvd_flags.fetch_cvd_flags.instances) {
      if (instance.should_fetch) {
        fetch_instances.emplace_back(instance);
      }
    }
    if (fetch_instances.size() > 0) {
      auto& fetch_cmd = *req_protos.emplace_back().mutable_command_request();
      *fetch_cmd.mutable_env() = client_env;
      AddFetchCommandArgs(fetch_cmd, cvd_flags.fetch_cvd_flags, fetch_instances,
                          load_directories);
    }

    auto& mkdir_cmd = *req_protos.emplace_back().mutable_command_request();
    *mkdir_cmd.mutable_env() = client_env;
    mkdir_cmd.add_args("cvd");
    mkdir_cmd.add_args("mkdir");
    mkdir_cmd.add_args("-p");
    mkdir_cmd.add_args(load_directories.launch_home_directory);

    auto& launch_cmd = *req_protos.emplace_back().mutable_command_request();
    launch_cmd.set_working_directory(load_directories.first_instance_directory);
    *launch_cmd.mutable_env() = client_env;
    (*launch_cmd.mutable_env())["HOME"] =
        load_directories.launch_home_directory;
    (*launch_cmd.mutable_env())[kAndroidHostOut] =
        load_directories.first_instance_directory;
    (*launch_cmd.mutable_env())[kAndroidSoongHostOut] =
        load_directories.first_instance_directory;
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
    launch_cmd.add_args(load_directories.system_image_directory_flag);

    launch_cmd.mutable_selector_opts()->add_args(
        std::string("--") + selector::SelectorFlags::kDisableDefaultGroup);

    /*Verbose is disabled by default*/
    auto dev_null = SharedFD::Open("/dev/null", O_RDWR);
    CF_EXPECT(dev_null->IsOpen(), dev_null->StrError());
    std::vector<SharedFD> fds = {dev_null, dev_null, dev_null};
    std::vector<RequestWithStdio> ret;

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

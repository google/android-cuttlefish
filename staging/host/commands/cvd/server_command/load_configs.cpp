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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <json/json.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

namespace {

constexpr std::string_view kCredentialSourceOverride =
    "fetch.credential_source=";

struct LoadFlags {
  bool help = false;
  std::vector<std::string> overrides;
  std::string config_path;
  std::string credential_source;
  std::string base_dir;
};

std::vector<Flag> GetFlagsVector(LoadFlags& load_flags) {
  std::vector<Flag> flags;
  flags.emplace_back(GflagsCompatFlag("help", load_flags.help));
  flags.emplace_back(
      GflagsCompatFlag("credential_source", load_flags.credential_source));
  flags.emplace_back(
      GflagsCompatFlag("base_directory", load_flags.base_dir)
          .Help("Parent directory for artifacts and runtime files. Defaults to "
                "/tmp/cvd/<uid>/<timestamp>."));
  FlagAlias alias = {FlagAliasMode::kFlagPrefix, "--override="};
  flags.emplace_back(Flag().Alias(alias).Setter(
      [&overrides = load_flags.overrides](const FlagMatch& m) -> Result<void> {
        overrides.push_back(m.value);
        return {};
      }));
  return flags;
}

std::string DefaultBaseDir() {
    auto time = std::chrono::system_clock::now().time_since_epoch().count();
    std::stringstream ss;
    ss << "/tmp/cvd/" << getuid() << "/" << time;
    return ss.str();
}

void MakeAbsolute(std::string& path, const std::string& working_dir) {
    if (path.size() > 0 && path[0] == '/') {
      return;
    }
    path.insert(0, working_dir + "/");
}

Result<LoadFlags> GetFlags(const RequestWithStdio& request) {
  LoadFlags load_flags;
  auto flags = GetFlagsVector(load_flags);
  auto args = ParseInvocation(request.Message()).arguments;
  CF_EXPECT(ParseFlags(flags, args));
  CF_EXPECT(load_flags.help || args.size() > 0,
            "No arguments provided to cvd load command, please provide at "
            "least one argument (help or path to json file)");
  auto working_directory = request.Message().command_request().working_directory();

  if (load_flags.base_dir.empty()) {
    load_flags.base_dir = DefaultBaseDir();
  }
  MakeAbsolute(load_flags.base_dir, working_directory);

  load_flags.config_path = args.front();
  MakeAbsolute(load_flags.config_path, working_directory);

  if (!load_flags.credential_source.empty()) {
    for (const auto& name : load_flags.overrides) {
      CF_EXPECT(!android::base::StartsWith(name, kCredentialSourceOverride),
                "Specifying both --override=fetch.credential_source and the "
                "--credential_source flag is not allowed.");
    }
    load_flags.overrides.emplace_back(std::string(kCredentialSourceOverride) +
                                      load_flags.credential_source);
  }
  return load_flags;
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
    const auto flags = CF_EXPECT(GetFlags(request));

    if (flags.help) {
      std::stringstream help_msg_stream;
      help_msg_stream << "Usage: cvd " << kLoadSubCmd << "\n";
      const auto help_msg = help_msg_stream.str();
      CF_EXPECT(WriteAll(request.Out(), help_msg) == help_msg.size());
      return {};
    }

    Json::Value json_configs =
        CF_EXPECT(GetOverridedJsonConfig(flags.config_path, flags.overrides));
    const auto load_directories =
        CF_EXPECT(GenerateLoadDirectories(flags.base_dir, json_configs["instances"].size()));
    auto cvd_flags = CF_EXPECT(ParseCvdConfigs(json_configs, load_directories),
                               "parsing json configs failed");
    std::vector<cvd::Request> req_protos;
    const auto& client_env = request.Message().command_request().env();

    if (!cvd_flags.fetch_cvd_flags.empty()) {
      auto& fetch_cmd = *req_protos.emplace_back().mutable_command_request();
      *fetch_cmd.mutable_env() = client_env;
      fetch_cmd.add_args("cvd");
      fetch_cmd.add_args("fetch");
      for (const auto& flag : cvd_flags.fetch_cvd_flags) {
        fetch_cmd.add_args(flag);
      }
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

    /* cvd load will always create instances in daemon mode (to be independent
     of terminal) and will enable reporting automatically (to run automatically
     without question during launch)
     */
    launch_cmd.add_args("cvd");
    launch_cmd.add_args("start");
    launch_cmd.add_args("--daemon");
    for (const auto& parsed_flag : cvd_flags.launch_cvd_flags) {
      launch_cmd.add_args(parsed_flag);
    }
    // Add system flag for multi-build scenario
    launch_cmd.add_args(load_directories.system_image_directory_flag);

    auto selector_opts = launch_cmd.mutable_selector_opts();

    for (const auto& flag: cvd_flags.selector_flags) {
      selector_opts->add_args(flag);
    }

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

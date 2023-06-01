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
#include <mutex>
#include <sstream>
#include <string>

#include <fruit/fruit.h>
#include <android-base/parseint.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/load_configs_parser.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_client.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

namespace {

using DemoCommandSequence = std::vector<RequestWithStdio>;

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

  // TODO: expand this enum in the future to support more types ( double , float
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
      std::uint32_t leaf_val;
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
        std::uint32_t index_val;
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

  bool HasValidDotSeparatedPrefix(const std::string& str) {
    auto equalsPos = str.find('=');
    if (equalsPos == std::string::npos) {
      return false;
    }
    std::string prefix = str.substr(0, equalsPos);
    // return false if prefix is empty, has no dots, start with dots, end with dot
    // or has cosequence of dots
    if (prefix.empty() || prefix.find('.') == std::string::npos ||
        prefix.find('.') == 0 || prefix.find("..") != std::string::npos ||
        prefix.back() == '.') {
      return false;
    }
    return true;
  }

  bool hasEqualsWithValidDotSeparatedPrefix(const std::string& str) {
    auto equalsPos = str.find('=');
    return equalsPos != std::string::npos && equalsPos < str.length() - 1 &&
           HasValidDotSeparatedPrefix(str);
  }

  bool ValidateArgsFormat(const std::vector<std::string>& strings) {
    for (const auto& str : strings) {
      if (!hasEqualsWithValidDotSeparatedPrefix(str)) {
        LOG(ERROR) << "Invalid  argument format. " << str
                   << " Please use arg=value";
        return false;
      }
    }
    return true;
  }

  Result<DemoCommandSequence> CreateCommandSequence(
      const RequestWithStdio& request) {
    bool help = false;

    std::vector<Flag> flags;
    flags.emplace_back(GflagsCompatFlag("help", help));
    std::string config_path;
    flags.emplace_back(GflagsCompatFlag("config_path", config_path));

    auto args = ParseInvocation(request.Message()).arguments;
    CF_EXPECT(ParseFlags(flags, args));

    Json::Value json_configs;
    if (help) {
      std::stringstream help_msg_stream;
      help_msg_stream << "Usage: cvd " << kLoadSubCmd << std::endl;
      const auto help_msg = help_msg_stream.str();
      CF_EXPECT(WriteAll(request.Out(), help_msg) == help_msg.size());
      return {};
    } else {
      json_configs =
          CF_EXPECT(ParseJsonFile(config_path), "parsing input file failed");

      if (args.size() > 0) {
        for (auto& single_arg : args) {
          LOG(INFO) << "Filtered args " << single_arg;
        }
        // Validate all arguments follow specific pattern
        if (!ValidateArgsFormat(args)) {
          return {};
        }
        // Convert all arguments to json tree
        auto args_tree = ParseArgsToJson(args);
        MergeTwoJsonObjs(json_configs, args_tree);
      }
    }

    auto cvd_flags =
        CF_EXPECT(ParseCvdConfigs(json_configs), "parsing json configs failed");

    std::vector<cvd::Request> req_protos;

    auto& launch_cmd = *req_protos.emplace_back().mutable_command_request();
    launch_cmd.set_working_directory(
        request.Message().command_request().working_directory());
    *launch_cmd.mutable_env() = request.Message().command_request().env();

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

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

#include <iostream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/display.h"

namespace cuttlefish {
namespace {

static const std::string kUsage =
    R"(Cuttlefish Virtual Device (CVD) Display CLI.

usage: cvd display <command> <args>

Commands:
    help                Print this message.
    help <command>      Print help for a command.
    add                 Adds a new display to a given device.
    list                Prints the currently connected displays.
    remove              Removes a display from a given device.
)";

static const std::string kAddUsage =
    R"(Cuttlefish Virtual Device (CVD) Display CLI.

Adds and connects a display to the given virtual device.

usage: cvd display add --width=720 --height=1280

       cvd display add \\
        --display=width=1280,height=800 \\
        --display=width=1920,height=1080,refresh_rate_hz=60
)";

static const std::string kListUsage =
    R"(Cuttlefish Virtual Device (CVD) Display CLI.

Lists all of the displays currently connected to a given virtual device.

usage: cvd display list
)";

static const std::string kRemoveUsage =
    R"(Cuttlefish Virtual Device (CVD) Display CLI.

Disconnects and removes displays from the given virtual device.

usage: cvd display remove \\
        --display=<display id> \\
        --display=<display id> ...
)";

static const std::unordered_map<std::string, std::string> kSubCommandUsages = {
    {"add", kAddUsage},
    {"list", kListUsage},
    {"help", kUsage},
    {"remove", kRemoveUsage},
};

Result<int> RunCrosvmDisplayCommand(int instance_num,
                                    const std::vector<std::string>& args) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  if (!config) {
    return CF_ERR("Failed to get Cuttlefish config.");
  }
  // TODO(b/260649774): Consistent executable API for selecting an instance
  auto instance = config->ForInstance(instance_num);

  const std::string crosvm_binary_path = instance.crosvm_binary();
  const std::string crosvm_control_path =
      instance.PerInstanceInternalUdsPath("crosvm_control.sock");

  cuttlefish::Command command(crosvm_binary_path);
  command.AddParameter("gpu");
  for (const std::string& arg : args) {
    command.AddParameter(arg);
  }
  command.AddParameter(crosvm_control_path);

  std::string out;
  std::string err;
  auto ret = RunWithManagedStdio(std::move(command), NULL, &out, &err);
  if (ret != 0) {
    std::cerr << "Failed to run crosvm display command: ret code: " << ret
              << "\n"
              << out << "\n"
              << err;
    return ret;
  }

  std::cerr << err << std::endl;
  std::cout << out << std::endl;
  return 0;
}

Result<int> GetInstanceNum(std::vector<std::string>& args) {
  int instance_num = 1;
  CF_EXPECT(ParseFlags({GflagsCompatFlag("instance_num", instance_num)}, args));
  return instance_num;
}

Result<int> DoHelp(std::vector<std::string>& args) {
  if (args.empty()) {
    std::cout << kUsage << std::endl;
    return 0;
  }

  const std::string& subcommand_str = args[0];
  auto subcommand_usage = kSubCommandUsages.find(subcommand_str);
  if (subcommand_usage == kSubCommandUsages.end()) {
    std::cerr << "Unknown subcommand '" << subcommand_str
              << "'. See `cvd display help`" << std::endl;
    return 1;
  }

  std::cout << subcommand_usage->second << std::endl;
  return 0;
}

Result<int> DoAdd(std::vector<std::string>& args) {
  const int instance_num = CF_EXPECT(GetInstanceNum(args));

  const auto display_configs = CF_EXPECT(ParseDisplayConfigsFromArgs(args));
  if (display_configs.empty()) {
    std::cerr << "Must provide at least 1 display to add. Usage:" << std::endl;
    std::cerr << kAddUsage << std::endl;
    return 1;
  }

  std::vector<std::string> add_displays_command_args;
  add_displays_command_args.push_back("add-displays");

  for (const auto& display_config : display_configs) {
    const std::string w = std::to_string(display_config.width);
    const std::string h = std::to_string(display_config.height);
    const std::string dpi = std::to_string(display_config.dpi);
    const std::string rr = std::to_string(display_config.refresh_rate_hz);

    const std::string add_display_flag =
        "--gpu-display=" + android::base::Join(
                               std::vector<std::string>{
                                   "mode=windowed[" + w + "," + h + "]",
                                   "dpi=[" + dpi + "," + dpi + "]",
                                   "refresh-rate=" + rr,
                               },
                               ",");

    add_displays_command_args.push_back(add_display_flag);
  }

  return CF_EXPECT(
      RunCrosvmDisplayCommand(instance_num, add_displays_command_args));
}

Result<int> DoList(std::vector<std::string>& args) {
  const int instance_num = CF_EXPECT(GetInstanceNum(args));
  return CF_EXPECT(RunCrosvmDisplayCommand(instance_num, {"list-displays"}));
}

Result<int> DoRemove(std::vector<std::string>& args) {
  const int instance_num = CF_EXPECT(GetInstanceNum(args));

  std::vector<std::string> displays;
  const std::vector<Flag> remove_displays_flags = {
      GflagsCompatFlag(kDisplayFlag)
          .Help("Display id of a display to remove.")
          .Setter([&](const FlagMatch& match) {
            displays.push_back(match.value);
            return true;
          }),
  };
  if (!ParseFlags(remove_displays_flags, args)) {
    std::cerr << "Failed to parse flags. Usage:" << std::endl;
    std::cerr << kRemoveUsage << std::endl;
    return 1;
  }

  if (displays.empty()) {
    std::cerr << "Must specify at least one display id to remove. Usage:"
              << std::endl;
    std::cerr << kRemoveUsage << std::endl;
    return 1;
  }

  std::vector<std::string> remove_displays_command_args;
  remove_displays_command_args.push_back("remove-displays");
  for (const auto& display : displays) {
    remove_displays_command_args.push_back("--display-id=" + display);
  }

  return CF_EXPECT(
      RunCrosvmDisplayCommand(instance_num, remove_displays_command_args));
}

using DisplaySubCommand = Result<int> (*)(std::vector<std::string>&);

int DisplayMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  const std::unordered_map<std::string, DisplaySubCommand> kSubCommands = {
      {"add", DoAdd},
      {"list", DoList},
      {"help", DoHelp},
      {"remove", DoRemove},
  };

  auto args = ArgsToVec(argc - 1, argv + 1);
  if (args.empty()) {
    args.push_back("help");
  }

  const std::string command_str = args[0];
  args.erase(args.begin());

  auto command_func_it = kSubCommands.find(command_str);
  if (command_func_it == kSubCommands.end()) {
    std::cerr << "Unknown display command: '" << command_str << "'."
              << std::endl;
    return 1;
  }

  auto result = command_func_it->second(args);
  if (!result.ok()) {
    std::cerr << result.error().Message();
    return 1;
  }
  return result.value();
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::DisplayMain(argc, argv); }

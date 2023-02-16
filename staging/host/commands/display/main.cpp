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
#include <gflags/gflags.h>

#include "common/libs/utils/subprocess.h"
#include "host/commands/assemble_cvd/display_flags.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_uint32(instance_num, 1, "Which instance to read the configs from");
DEFINE_uint32(width, 0,
              "When adding a display, the width of the display in pixels");
DEFINE_uint32(height, 0,
              "When adding a display, the height of the display in pixels");
DEFINE_uint32(dpi, 0,
              "When adding a display, the pixels per inch of the display");
DEFINE_uint32(refresh_rate_hz, 0,
              "When adding a display, the refresh rate of the display in "
              "Hertz");

DEFINE_string(display0, "", cuttlefish::kDisplayHelp);
DEFINE_string(display1, "", cuttlefish::kDisplayHelp);
DEFINE_string(display2, "", cuttlefish::kDisplayHelp);
DEFINE_string(display3, "", cuttlefish::kDisplayHelp);

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
        --display0=width=1280,height=800
        --display1=width=1920,height=1080,refresh_rate_hz=60
)";

static const std::string kListUsage =
    R"(Cuttlefish Virtual Device (CVD) Display CLI.

Lists all of the displays currently connected to a given virtual device.

usage: cvd display list
)";

static const std::string kRemoveUsage =
    R"(Cuttlefish Virtual Device (CVD) Display CLI.

Disconnects and removes a display from the given virtual device.

usage: cvd display remove <display index>
       cvd display remove <display index> <display index> ...
)";

static const std::unordered_map<std::string, std::string> kSubCommandUsages = {
    {"add", kAddUsage},
    {"list", kListUsage},
    {"help", kUsage},
    {"remove", kRemoveUsage},
};

Result<int> RunCrosvmDisplayCommand(const std::vector<std::string>& args) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  if (!config) {
    return CF_ERR("Failed to get Cuttlefish config.");
  }
  // TODO(b/260649774): Consistent executable API for selecting an instance
  auto instance = config->ForInstance(FLAGS_instance_num);

  const std::string crosvm_binary_path = instance.crosvm_binary();
  const std::string crosvm_control_path =
      instance.PerInstanceInternalPath("crosvm_control.sock");

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

Result<int> DoHelp(const std::vector<std::string>& args) {
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

Result<std::optional<CuttlefishConfig::DisplayConfig>>
ParseLegacyDisplayFlags() {
  if (FLAGS_width == 0 && FLAGS_height == 0 && FLAGS_dpi == 0 &&
      FLAGS_refresh_rate_hz == 0) {
    return std::nullopt;
  }

  CF_EXPECT_GT(FLAGS_width, 0,
               "Must specify valid --width flag. Usage:\n"
                   << kAddUsage);
  CF_EXPECT_GT(FLAGS_height, 0,
               "Must specify valid --height flag. Usage:\n"
                   << kAddUsage);
  CF_EXPECT_GT(FLAGS_dpi, 0,
               "Must specify valid --dpi flag. Usage:\n"
                   << kAddUsage);
  CF_EXPECT_GT(FLAGS_refresh_rate_hz, 0,
               "Must specify valid --refresh_rate_hz flag. Usage:\n"
                   << kAddUsage);

  const int display_width = FLAGS_width;
  const int display_height = FLAGS_height;
  const int display_dpi = FLAGS_dpi > 0 ? FLAGS_dpi : CF_DEFAULTS_DISPLAY_DPI;
  const int display_rr = FLAGS_refresh_rate_hz > 0
                             ? FLAGS_refresh_rate_hz
                             : CF_DEFAULTS_DISPLAY_REFRESH_RATE;

  return CuttlefishConfig::DisplayConfig{
      .width = display_width,
      .height = display_height,
      .dpi = display_dpi,
      .refresh_rate_hz = display_rr,
  };
}

Result<int> DoAdd(const std::vector<std::string>&) {
  std::vector<CuttlefishConfig::DisplayConfig> display_configs;

  auto display = CF_EXPECT(ParseLegacyDisplayFlags());
  if (display) {
    display_configs.push_back(*display);
  }
  auto display0 = CF_EXPECT(ParseDisplayConfig(FLAGS_display0));
  if (display0) {
    display_configs.push_back(*display0);
  }
  auto display1 = CF_EXPECT(ParseDisplayConfig(FLAGS_display1));
  if (display1) {
    display_configs.push_back(*display1);
  }
  auto display2 = CF_EXPECT(ParseDisplayConfig(FLAGS_display2));
  if (display2) {
    display_configs.push_back(*display2);
  }
  auto display3 = CF_EXPECT(ParseDisplayConfig(FLAGS_display3));
  if (display3) {
    display_configs.push_back(*display3);
  }

  if (display_configs.empty()) {
    return CF_ERR("No displays params provided. Usage:\n" << kAddUsage);
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

  return CF_EXPECT(RunCrosvmDisplayCommand(add_displays_command_args));
}

Result<int> DoList(const std::vector<std::string>&) {
  return CF_EXPECT(RunCrosvmDisplayCommand({"list-displays"}));
}

Result<int> DoRemove(const std::vector<std::string>& args) {
  if (args.empty()) {
    std::cerr << "Must specify the display id to remove. Usage:" << std::endl;
    std::cerr << kRemoveUsage << std::endl;
    return 1;
  }

  std::vector<std::string> remove_displays_command_args;
  remove_displays_command_args.push_back("remove-displays");
  for (const auto& arg : args) {
    remove_displays_command_args.push_back("--display-id=" + arg);
  }

  return CF_EXPECT(RunCrosvmDisplayCommand(remove_displays_command_args));
}

using DisplaySubCommand = Result<int> (*)(const std::vector<std::string>&);

int DisplayMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::gflags::SetUsageMessage(kUsage);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.push_back(argv[i]);
  }

  if (args.empty()) {
    args.push_back("help");
  }

  const std::unordered_map<std::string, DisplaySubCommand> kSubCommands = {
      {"add", DoAdd},
      {"list", DoList},
      {"help", DoHelp},
      {"remove", DoRemove},
  };

  const auto command_str = args[0];
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

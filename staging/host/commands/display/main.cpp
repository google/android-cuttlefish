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
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_uint32(instance_num, 1, "Which instance to read the configs from");
DEFINE_uint32(width, 0,
              "When adding a display, the width of the display in pixels");
DEFINE_uint32(height, 0,
              "When adding a display, the height of the display in pixels");
DEFINE_uint32(dpi, 320,
              "When adding a display, the pixels per inch of the display");
DEFINE_uint32(refresh_rate_hz, 60,
              "When adding a display, the refresh rate of the display in "
              "Hertz");

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
)";

static const std::unordered_map<std::string, std::string> kSubCommandUsages = {
    {"add", kAddUsage},
    {"list", kListUsage},
    {"help", kUsage},
    {"remove", kRemoveUsage},
};

int RunCrosvmDisplayCommand(const std::vector<std::string>& args) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  if (!config) {
    LOG(FATAL) << "Failed to get Cuttlefish config.";
  }

  const std::string crosvm_binary_path = config->crosvm_binary();
  const std::string crosvm_control_path =
      config->ForInstance(FLAGS_instance_num)
          .PerInstanceInternalPath("crosvm_control.sock");

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

int DoHelp(const std::vector<std::string>& args) {
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

int DoAdd(const std::vector<std::string>&) {
  if (FLAGS_width <= 0) {
    std::cerr << "Must specify valid --width flag. Usage:" << std::endl;
    std::cerr << kAddUsage << std::endl;
    return 1;
  }
  if (FLAGS_height <= 0) {
    std::cerr << "Must specify valid --height flag. Usage:" << std::endl;
    std::cerr << kAddUsage << std::endl;
    return 1;
  }
  if (FLAGS_dpi <= 0) {
    std::cerr << "Must specify valid --dpi flag. Usage:" << std::endl;
    std::cerr << kAddUsage << std::endl;
    return 1;
  }
  if (FLAGS_refresh_rate_hz <= 0) {
    std::cerr << "Must specify valid --dpi flag. Usage:" << std::endl;
    std::cerr << kAddUsage << std::endl;
    return 1;
  }

  const std::string w = std::to_string(FLAGS_width);
  const std::string h = std::to_string(FLAGS_height);
  const std::string dpi = std::to_string(FLAGS_dpi);
  const std::string rr = std::to_string(FLAGS_refresh_rate_hz);

  const std::string params = android::base::Join(
      std::vector<std::string>{
          "mode=windowed[" + w + "," + h + "]",
          "dpi=[" + dpi + "," + dpi + "]",
          "refresh-rate=" + rr,
      },
      ",");

  return RunCrosvmDisplayCommand({
      "add-displays",
      "--gpu-display=" + params,
  });
}

int DoList(const std::vector<std::string>&) {
  return RunCrosvmDisplayCommand({"list-displays"});
}

int DoRemove(const std::vector<std::string>& args) {
  if (args.empty()) {
    std::cerr << "Must specify the display id to remove. Usage:" << std::endl;
    std::cerr << kRemoveUsage << std::endl;
    return 1;
  }

  return RunCrosvmDisplayCommand({
      "remove-displays",
      "--display-id=" + args[0],
  });
}

using DisplaySubCommand = int (*)(const std::vector<std::string>&);

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

  return command_func_it->second(args);
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::DisplayMain(argc, argv); }

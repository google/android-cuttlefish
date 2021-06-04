/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include <map>
#include <string>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

std::string kStartBin = HostBinaryPath("cvd_internal_start");
std::string kStopBin = HostBinaryPath("cvd_internal_stop");

constexpr char kHelpBin[] = "help_placeholder";  // Unused, prints kHelpMessage.
constexpr char kHelpMessage[] = R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd <command>

Commands:
  help                Print this message.
  help <command>      Print help for a command.
  start               Start a device.
  stop                Stop a running device.
)";

const std::map<std::string, std::string> CommandToBinaryMap = {
    {"help", kHelpBin},
    {"start", kStartBin},
    {"launch_cvd", kStartBin},
    {"stop", kStopBin},
    {"stop_cvd", kStopBin}};

int CvdMain(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  std::vector<Flag> flags;

  std::string bin;
  std::string program_name = cpp_basename(argv[0]);
  std::string subcommand_name = program_name;
  if (program_name == "cvd") {
    if (argc == 1) {
      // Show help if user invokes `cvd` alone.
      subcommand_name = "help";
    } else {
      subcommand_name = argv[1];
    }
  }
  auto subcommand_bin = CommandToBinaryMap.find(subcommand_name);
  if (subcommand_bin == CommandToBinaryMap.end()) {
    // Show help if subcommand not found.
    bin = kHelpBin;
  } else {
    bin = subcommand_bin->second;
  }

  // Allow `cvd --help` and `cvd help --help`.
  if (bin == kHelpBin) {
    flags.emplace_back(HelpFlag(flags, kHelpMessage));
  }

  // Collect args, skipping the program name.
  size_t args_to_skip = program_name == "cvd" ? 2 : 1;
  std::vector<std::string> args =
      ArgsToVec(argc - args_to_skip, argv + args_to_skip);
  CHECK(ParseFlags(flags, args));

  if (bin == kHelpBin) {
    // Handle `cvd help`
    if (args.empty()) {
      std::cout << kHelpMessage;
      return 0;
    }

    // Handle `cvd help <subcommand>` by calling the subcommand with --help.
    auto it = CommandToBinaryMap.find(args[0]);
    if (it == CommandToBinaryMap.end() || args[0] == "help") {
      std::cout << kHelpMessage;
      return 0;
    }
    bin = it->second;
    args = {"--help"};
  }

  Command command(bin);
  for (const std::string& arg : args) {
    command.AddParameter(arg);
  }
  return command.Start().Wait();
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) { return cuttlefish::CvdMain(argc, argv); }

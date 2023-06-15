/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/snapshot_util_cvd/parse.h"

#include <iostream>
#include <unordered_map>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

constexpr char snapshot_cmd_help[] =
    "Command to control regarding the snapshot operations: "
    "suspend/resume/take";

constexpr char instance_num_help[] = "Which instance to suspend.";

constexpr char wait_for_launcher_help[] =
    "How many seconds to wait for the launcher to respond to the status "
    "command. A value of zero means wait indefinitely.";

Flag SnapshotCmdFlag(std::string& value_buf) {
  return GflagsCompatFlag("subcmd", value_buf).Help(snapshot_cmd_help);
}

Flag GetInt32Flag(const std::string& name, int& value_buf,
                  const std::string& help_msg) {
  return GflagsCompatFlag(name, value_buf).Help(help_msg);
}

Flag InstanceNumFlag(int& instance_num) {
  return GetInt32Flag("instance_num", instance_num, instance_num_help);
}

Flag WaitForLauncherFlag(int& wait_for_launcher) {
  return GetInt32Flag("wait_for_launcher", wait_for_launcher,
                      wait_for_launcher_help);
}

}  // namespace

Result<Parsed> Parse(int argc, char** argv) {
  auto args = ArgsToVec(argc, argv);
  auto parsed = CF_EXPECT(Parse(args));
  return parsed;
}

Result<SnapshotCmd> ConvertToSnapshotCmd(const std::string& input) {
  std::unordered_map<std::string, SnapshotCmd> mapping{
      {"suspend", SnapshotCmd::kSuspend},   {"resume", SnapshotCmd::kResume},
      {"take", SnapshotCmd::kSnapshotTake}, {"unset", SnapshotCmd::kUnknown},
      {"unknown", SnapshotCmd::kUnknown},
  };
  CF_EXPECT(Contains(mapping, input));
  return mapping.at(input);
}

Result<Parsed> Parse(std::vector<std::string>& args) {
  Parsed parsed{
      .instance_num = GetInstance(),
      .wait_for_launcher = 30,
  };
  std::vector<Flag> flags;
  bool help_xml = false;
  std::string snapshot_op("unknown");
  flags.push_back(SnapshotCmdFlag(snapshot_op));
  flags.push_back(InstanceNumFlag(parsed.instance_num));
  flags.push_back(WaitForLauncherFlag(parsed.wait_for_launcher));
  flags.push_back(HelpFlag(flags));
  flags.push_back(HelpXmlFlag(flags, std::cout, help_xml));
  flags.push_back(UnexpectedArgumentGuard());
  CF_EXPECT(ParseFlags(flags, args), "Flag parsing failed");
  parsed.cmd = CF_EXPECT(ConvertToSnapshotCmd(snapshot_op));
  return parsed;
}

std::ostream& operator<<(std::ostream& out, const SnapshotCmd& cmd) {
  switch (cmd) {
    case SnapshotCmd::kUnknown:
      out << "unknown";
      break;
    case SnapshotCmd::kSuspend:
      out << "suspend";
      break;
    case SnapshotCmd::kResume:
      out << "resume";
      break;
    case SnapshotCmd::kSnapshotTake:
      out << "snapshot take";
      break;
    default:
      out << "unknown";
      break;
  }
  return out;
}

}  // namespace cuttlefish

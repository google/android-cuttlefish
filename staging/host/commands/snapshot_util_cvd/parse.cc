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

#include <cstdlib>
#include <iostream>
#include <unordered_map>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

constexpr char snapshot_cmd_help[] =
    "Command to control regarding the snapshot operations: "
    "suspend/resume/snapshot_take";

constexpr char cleanup_snapshot_path_help[] =
    "If true, snapshot_util_cvd will clean up the snapshot path on "
    "failure of snapshot-taking";

constexpr char wait_for_launcher_help[] =
    "How many seconds to wait for the launcher to respond to the status "
    "command. A value of zero means wait indefinitely.";

constexpr char snapshot_path_help[] =
    "Path to the directory the taken snapshot files are saved";

Flag SnapshotCmdFlag(std::string& value_buf) {
  return GflagsCompatFlag("subcmd", value_buf).Help(snapshot_cmd_help);
}

Flag GetInt32Flag(const std::string& name, int& value_buf,
                  const std::string& help_msg) {
  return GflagsCompatFlag(name, value_buf).Help(help_msg);
}

Flag WaitForLauncherFlag(int& wait_for_launcher) {
  return GetInt32Flag("wait_for_launcher", wait_for_launcher,
                      wait_for_launcher_help);
}

Flag SnapshotPathFlag(std::string& path_buf) {
  return GflagsCompatFlag("snapshot_path", path_buf).Help(snapshot_path_help);
}

Flag CleanupSnapshotPathFlag(bool& cleanup) {
  return GflagsCompatFlag("cleanup_snapshot_path", cleanup)
      .Help(cleanup_snapshot_path_help);
}

}  // namespace

Result<Parsed> Parse(int argc, char** argv) {
  auto args = ArgsToVec(argc, argv);
  auto parsed = CF_EXPECT(Parse(args));
  return parsed;
}

Result<SnapshotCmd> ConvertToSnapshotCmd(const std::string& input) {
  std::unordered_map<std::string, SnapshotCmd> mapping{
      {"suspend", SnapshotCmd::kSuspend},
      {"resume", SnapshotCmd::kResume},
      {"snapshot_take", SnapshotCmd::kSnapshotTake},
      {"unknown", SnapshotCmd::kUnknown},
  };
  CF_EXPECT(Contains(mapping, input));
  return mapping.at(input);
}

static Result<std::vector<int>> InstanceNums() {
  CF_EXPECT(getenv("HOME") != nullptr, "\"HOME\" must be set properly.");
  const auto* config = CuttlefishConfig::Get();
  CF_EXPECT(config != nullptr, "CuttlefishConfig::Get() returned nullptr");

  const auto instances = config->Instances();
  std::vector<int> instance_nums;
  CF_EXPECT(!instances.empty(), "CuttlefishConfig has no instance in it.");
  instance_nums.reserve(instances.size());
  for (const auto& instance : instances) {
    int id;
    CF_EXPECTF(android::base::ParseInt(instance.id(), &id),
               "Parsing filed for {}", id);
    instance_nums.push_back(id);
  }
  return instance_nums;
}

Result<Parsed> Parse(std::vector<std::string>& args) {
  Parsed parsed{
      .wait_for_launcher = 30,
      .cleanup_snapshot_path = true,
  };
  std::vector<Flag> flags;
  bool help_xml = false;
  std::string snapshot_op("unknown");
  std::string snapshot_path;
  flags.push_back(SnapshotCmdFlag(snapshot_op));
  flags.push_back(WaitForLauncherFlag(parsed.wait_for_launcher));
  flags.push_back(SnapshotPathFlag(snapshot_path));
  flags.push_back(CleanupSnapshotPathFlag(parsed.cleanup_snapshot_path));
  flags.push_back(
      GflagsCompatFlag("force", parsed.force)
          .Help("If the snapshot path already exists, delete it first"));
  flags.push_back(GflagsCompatFlag("auto_suspend", parsed.auto_suspend)
                      .Help("Suspend/resume before/after taking the snapshot"));
  flags.push_back(HelpFlag(flags));
  flags.push_back(HelpXmlFlag(flags, std::cout, help_xml));
  flags.push_back(UnexpectedArgumentGuard());
  CF_EXPECT(ConsumeFlags(flags, args), "Flag parsing failed");
  parsed.cmd = CF_EXPECT(ConvertToSnapshotCmd(snapshot_op));
  parsed.snapshot_path = snapshot_path;
  parsed.instance_nums = CF_EXPECT(InstanceNums());
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
      out << "snapshot_take";
      break;
    default:
      out << "unknown";
      break;
  }
  return out;
}

}  // namespace cuttlefish

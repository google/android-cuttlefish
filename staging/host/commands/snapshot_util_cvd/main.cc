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

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/scopeguard.h>
#include <fmt/core.h>
#include <google/protobuf/text_format.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/snapshot_util_cvd/parse.h"
#include "host/commands/snapshot_util_cvd/snapshot_taker.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"
#include "run_cvd.pb.h"

namespace cuttlefish {
namespace {

Result<std::string> ToAbsolutePath(const std::string& snapshot_path) {
  const InputPathForm default_path_form{
      .current_working_dir = std::nullopt,
      .home_dir = std::nullopt,
      .path_to_convert = snapshot_path,
      .follow_symlink = false,
  };
  return CF_EXPECTF(
      EmulateAbsolutePath(default_path_form),
      "The snapshot path, \"{}\", cannot be converted to an absolute path",
      snapshot_path);
}

// Send a `LauncherAction` RPC to every instance specified in `parsed`.
Result<void> BroadcastLauncherAction(
    const CuttlefishConfig& config, const Parsed& parsed,
    run_cvd::ExtendedLauncherAction extended_action) {
  for (const auto instance_num : parsed.instance_nums) {
    LOG(INFO) << "Instance #" << instance_num
              << ": Sending request: " << extended_action.ShortDebugString();
    auto socket = CF_EXPECT(
        GetLauncherMonitor(config, instance_num, parsed.wait_for_launcher));
    CF_EXPECT(RunLauncherAction(socket, extended_action, std::nullopt));
  }
  return {};
}

Result<void> SnapshotCvdMain(std::vector<std::string> args) {
  const CuttlefishConfig* config =
      CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");

  CF_EXPECT(!args.empty(), "No arguments was given");
  const auto prog_path = args.front();
  args.erase(args.begin());
  auto parsed = CF_EXPECT(Parse(args));

  switch (parsed.cmd) {
    case SnapshotCmd::kSuspend: {
      run_cvd::ExtendedLauncherAction extended_action;
      extended_action.mutable_suspend();
      CF_EXPECT(BroadcastLauncherAction(*config, parsed, extended_action));
      return {};
    }
    case SnapshotCmd::kResume: {
      run_cvd::ExtendedLauncherAction extended_action;
      extended_action.mutable_resume();
      CF_EXPECT(BroadcastLauncherAction(*config, parsed, extended_action));
      return {};
    }
    case SnapshotCmd::kSnapshotTake: {
      CF_EXPECT(!parsed.snapshot_path.empty(), "--snapshot_path is required");
      parsed.snapshot_path = CF_EXPECT(ToAbsolutePath(parsed.snapshot_path));
      if (parsed.force &&
          FileExists(parsed.snapshot_path, /* follow symlink */ false)) {
        CF_EXPECTF(RecursivelyRemoveDirectory(parsed.snapshot_path),
                   "Failed to delete preexisting dir at {}",
                   parsed.snapshot_path);
      }
      CF_EXPECTF(!FileExists(parsed.snapshot_path, /* follow symlink */ false),
                 "Delete the destination directiory \"{}\" first",
                 parsed.snapshot_path);

      // Automically suspend and resume if requested.
      if (parsed.auto_suspend) {
        run_cvd::ExtendedLauncherAction extended_action;
        extended_action.mutable_suspend();
        CF_EXPECT(BroadcastLauncherAction(*config, parsed, extended_action));
      }
      auto maybe_resume_on_exit =
          android::base::ScopeGuard([&parsed, &config]() {
            if (!parsed.auto_suspend) {
              return;
            }
            run_cvd::ExtendedLauncherAction extended_action;
            extended_action.mutable_resume();
            Result<void> result =
                BroadcastLauncherAction(*config, parsed, extended_action);
            if (!result.ok()) {
              LOG(FATAL) << "RunLauncherAction failed: "
                         << result.error().FormatForEnv();
            }
          });

      // Delete incomplete snapshot if we fail partway.
      android::base::ScopeGuard delete_snapshot_on_fail([&parsed]() {
        if (!parsed.cleanup_snapshot_path) {
          return;
        }
        LOG(ERROR) << "Snapshot take failed, so running clean-up.";
        if (!RecursivelyRemoveDirectory(parsed.snapshot_path)) {
          LOG(ERROR) << "Failed to delete incomplete snapshot at "
                     << parsed.snapshot_path;
        }
      });

      // Snapshot group-level host runtime files and generate snapshot metadata
      // file.
      const std::string meta_json_path =
          CF_EXPECT(HandleHostGroupSnapshot(parsed.snapshot_path),
                    "Failed to back up the group-level host runtime files.");
      // Snapshot each instance.
      run_cvd::ExtendedLauncherAction extended_action;
      extended_action.mutable_snapshot_take()->set_snapshot_path(
          meta_json_path);
      CF_EXPECT(BroadcastLauncherAction(*config, parsed, extended_action));
      delete_snapshot_on_fail.Disable();
      return {};
    }
    default: {
      return CF_ERRF("unknown cmd: {}", (int)parsed.cmd);
    }
  }
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  std::vector<std::string> all_args = cuttlefish::ArgsToVec(argc, argv);
  auto result = cuttlefish::SnapshotCvdMain(std::move(all_args));
  if (!result.ok()) {
    LOG(ERROR) << result.error().FormatForEnv();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

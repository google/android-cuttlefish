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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/snapshot_util_cvd/parse.h"
#include "host/commands/snapshot_util_cvd/snapshot_taker.h"
#include "host/libs/command_util/runner/proto_utils.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"
#include "run_cvd.pb.h"

namespace cuttlefish {
namespace {

Result<RequestInfo> SerializeRequest(const SnapshotCmd subcmd,
                                     const std::string& meta_json_path) {
  switch (subcmd) {
    case SnapshotCmd::kSuspend: {
      return RequestInfo{
          .serialized_data = CF_EXPECT(SerializeSuspendRequest()),
          .extended_action_type = ExtendedActionType::kSuspend,
      };
      break;
    }
    case SnapshotCmd::kResume: {
      return RequestInfo{
          .serialized_data = CF_EXPECT(SerializeResumeRequest()),
          .extended_action_type = ExtendedActionType::kResume,
      };
      break;
    }
    case SnapshotCmd::kSnapshotTake: {
      return RequestInfo{
          .serialized_data =
              CF_EXPECT(SerializeSnapshotTakeRequest(meta_json_path)),
          .extended_action_type = ExtendedActionType::kSnapshotTake,
      };
      break;
    }
    default:
      return CF_ERR("Operation not supported.");
  }
}

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

static void OnSnapshotTakeFailure(const std::string& snapshot_path) {
  if (snapshot_path.empty()) {
    return;
  }
  LOG(DEBUG) << "Deleting " << snapshot_path << "....";
  RecursivelyRemoveDirectory(snapshot_path);
}

Result<void> SnapshotCvdMain(std::vector<std::string> args) {
  CF_EXPECT(!args.empty(), "No arguments was given");
  const auto prog_path = args.front();
  args.erase(args.begin());
  auto parsed = CF_EXPECT(Parse(args));
  if (!parsed.snapshot_path.empty()) {
    parsed.snapshot_path = CF_EXPECT(ToAbsolutePath(parsed.snapshot_path));
  }
  // make sure the snapshot directory exists
  std::string meta_json_path;
  if (parsed.cmd == SnapshotCmd::kSnapshotTake) {
    CF_EXPECT(!parsed.snapshot_path.empty(),
              "Snapshot operation requires snapshot path.");
    CF_EXPECTF(!FileExists(parsed.snapshot_path, /* follow symlink */ false),
               "Delete the destination directiory \"{}\" first",
               parsed.snapshot_path);
    android::base::ScopeGuard delete_snapshot_on_fail([&parsed]() {
      if (!parsed.cleanup_snapshot_path) {
        return;
      }
      LOG(ERROR) << "Snapshot take failed, so running clean-up.";
      OnSnapshotTakeFailure(parsed.snapshot_path);
    });
    if (!parsed.cleanup_snapshot_path) {
      delete_snapshot_on_fail.Disable();
    }
    meta_json_path =
        CF_EXPECT(HandleHostGroupSnapshot(parsed.snapshot_path),
                  "Failed to back up the group-level host runtime files.");
    delete_snapshot_on_fail.Disable();
  }

  const CuttlefishConfig* config =
      CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");
  // TODO(kwstephenkim): copy host files that are shared by the instance group
  for (const auto instance_num : parsed.instance_nums) {
    SharedFD monitor_socket = CF_EXPECT(
        GetLauncherMonitor(*config, instance_num, parsed.wait_for_launcher));

    LOG(INFO) << "Requesting " << parsed.cmd << " for instance #"
              << instance_num;

    android::base::ScopeGuard delete_snapshot_on_fail([&parsed]() {
      LOG(ERROR) << "Snapshot take failed, so running clean-up.";
      OnSnapshotTakeFailure(parsed.snapshot_path);
    });
    if (parsed.cmd != SnapshotCmd::kSnapshotTake) {
      delete_snapshot_on_fail.Disable();
    }

    auto [serialized_data, extended_type] =
        CF_EXPECT(SerializeRequest(parsed.cmd, meta_json_path));
    CF_EXPECT(RunLauncherAction(monitor_socket, extended_type,
                                std::move(serialized_data), std::nullopt));
    LOG(INFO) << parsed.cmd << " was successful for instance #" << instance_num;
    if (parsed.cmd == SnapshotCmd::kSnapshotTake) {
      delete_snapshot_on_fail.Disable();
    }
  }
  return {};
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

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

#include "host/commands/snapshot_util_cvd/snapshot_taker.h"

#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"
#include "host/libs/command_util/snapshot_utils.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

/**
 * cp -r <cuttlefish home dir> <snapshot_path>
 * write meta info for cvd: i.e. HOME, group name, instance names
 * returns the path of generated snapshot json file
 */
Result<std::string> HandleHostGroupSnapshot(const std::string& path) {
  const auto cuttlefish_home = StringFromEnv("HOME", "");
  CF_EXPECT(!cuttlefish_home.empty(),
            "\"HOME\" environment variable must be set.");

  const std::string snapshot_path = CF_EXPECT(EmulateAbsolutePath(InputPathForm{
      .current_working_dir = CurrentDirectory(),
      .home_dir = CF_EXPECT(SystemWideUserHome()),
      .path_to_convert = path,
      .follow_symlink = false,
  }));

  auto* cuttlefish_config = CuttlefishConfig::Get();
  CF_EXPECT(cuttlefish_config != nullptr, "Cannot find cuttlefish_config.json");

  const auto cuttlefish_root = cuttlefish_config->root_dir();
  CF_EXPECTF(android::base::StartsWith(cuttlefish_root, cuttlefish_home),
             "Cuttlefish Root directory \"{}\" "
             "is not subdirectory of cuttlefish home \"{}\".",
             cuttlefish_root, cuttlefish_home);

  // cp -r HOME snapshot_path
  auto always_true = [](const std::string&) -> bool { return true; };
  CF_EXPECTF(CopyDirectoryRecursively(cuttlefish_root, snapshot_path,
                                      /* verify_dest_dir_empty */ true,
                                      /* predicate */ std::move(always_true)),
             "\"cp -r {} {} failed.\"", cuttlefish_root, snapshot_path);

  const auto meta_json =
      CF_EXPECTF(CreateMetaInfo(*cuttlefish_config, snapshot_path),
                 "Failed to create ", kMetaInfoJsonFileName);
  const auto serialized_meta_json = meta_json.toStyledString();
  LOG(DEBUG) << "Generated " << kMetaInfoJsonFileName << ": " << std::endl
             << std::endl
             << serialized_meta_json;

  // write meta_json to file
  const auto meta_json_path = SnapshotMetaJsonPath(snapshot_path);
  CF_EXPECTF(
      android::base::WriteStringToFile(serialized_meta_json, meta_json_path,
                                       /* follow_symlinks */ true),
      "Failed to write the meta information in json to \"{}\"", meta_json_path);

  // mkdir guest_snapshot under the snapshot directory
  CF_EXPECT(meta_json.isMember(kGuestSnapshotField));
  auto meta_json_guest_snapshot = meta_json[kGuestSnapshotField];
  for (const auto& instance : cuttlefish_config->Instances()) {
    CF_EXPECT(meta_json_guest_snapshot.isMember(instance.id()));
    const std::string new_dir_path =
        snapshot_path + "/" +
        meta_json_guest_snapshot[instance.id()].asString();
    CF_EXPECTF(EnsureDirectoryExists(new_dir_path),
               "Failed to create instance guest snapshot directory {}",
               new_dir_path);
  }
  return meta_json_path;
}

}  // namespace cuttlefish

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

#include "host/libs/command_util/snapshot_utils.h"

#include <unistd.h>
#include <utime.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

bool IsFifo(const struct stat& file_stat) {
  return S_ISFIFO(file_stat.st_mode);
}

bool IsSocket(const struct stat& file_stat) {
  return S_ISSOCK(file_stat.st_mode);
}

bool IsSymlink(const struct stat& file_stat) {
  return S_ISLNK(file_stat.st_mode);
}

bool IsRegular(const struct stat& file_stat) {
  return S_ISREG(file_stat.st_mode);
}

// assumes that src_dir_path and dest_dir_path exist and both are
// existing directories or links to the directories. Also they are
// different directories.
Result<void> CopyDirectoryImpl(
    const std::string& src_dir_path, const std::string& dest_dir_path,
    const std::function<bool(const std::string&)>& predicate) {
  // create an empty dest_dir_path with the same permission as src_dir_path
  // and then, recursively copy the contents
  LOG(DEBUG) << "Making sure " << dest_dir_path
             << " exists and is effectively a directory.";
  CF_EXPECTF(EnsureDirectoryExists(dest_dir_path),
             "Directory {} cannot to be created; it does not exist, either.",
             dest_dir_path);
  const auto src_contents = CF_EXPECT(DirectoryContents(src_dir_path));
  for (const auto& src_base_path : src_contents) {
    if (!predicate(src_dir_path + "/" + src_base_path)) {
      continue;
    }
    if (src_base_path == "." || src_base_path == "..") {
      LOG(DEBUG) << "Skipping \"" << src_base_path << "\"";
      continue;
    }
    std::string src_path = src_dir_path + "/" + src_base_path;
    std::string dest_path = dest_dir_path + "/" + src_base_path;

    LOG(DEBUG) << "Handling... " << src_path;

    struct stat src_stat;
    CF_EXPECTF(lstat(src_path.data(), &src_stat) != -1, "Failed in lstat({})",
               src_path);
    if (IsSymlink(src_stat)) {
      std::string target;
      CF_EXPECTF(android::base::Readlink(src_path, &target),
                 "Readlink failed for {}", src_path);
      LOG(DEBUG) << "Creating link from " << dest_path << " to " << target;
      if (FileExists(dest_path, /* follow_symlink */ false)) {
        CF_EXPECTF(RemoveFile(dest_path), "Failed to unlink/remove file \"{}\"",
                   dest_path);
      }
      CF_EXPECTF(symlink(target.data(), dest_path.data()) == 0,
                 "Creating symbolic link from {} to {} failed: {}", dest_path,
                 target, strerror(errno));
      continue;
    }

    if (IsFifo(src_stat) || IsSocket(src_stat)) {
      LOG(DEBUG) << "Ignoring a named pipe or socket " << src_path;
      continue;
    }

    if (DirectoryExists(src_path)) {
      LOG(DEBUG) << "Recursively calling CopyDirectoryImpl(" << src_path << ", "
                 << dest_path << ")";
      CF_EXPECT(CopyDirectoryImpl(src_path, dest_path, predicate));
      LOG(DEBUG) << "Returned from Recursive call CopyDirectoryImpl("
                 << src_path << ", " << dest_path << ")";
      continue;
    }

    CF_EXPECTF(IsRegular(src_stat),
               "File {} must be directory, link, socket, pipe or regular."
               "{} is none of those",
               src_path, src_path);

    CF_EXPECTF(Copy(src_path, dest_path), "Copy from {} to {} failed", src_path,
               dest_path);

    auto dest_fd = SharedFD::Open(dest_path, O_RDONLY);
    CF_EXPECT(dest_fd->IsOpen(), "Failed to open \"" << dest_path << "\"");
    // Copy the mtime from the src file. The mtime of the disk image files can
    // be important because we later validate that the disk overlays are not
    // older than the disk components.
    const struct timespec times[2] = {
#if defined(__APPLE__)
      src_stat.st_atimespec,
      src_stat.st_mtimespec
#else
      src_stat.st_atim,
      src_stat.st_mtim,
#endif
    };
    if (dest_fd->Futimens(times) != 0) {
      return CF_ERR("futimens(\""
                    << dest_path << "\", ...) failed: " << dest_fd->StrError());
    }
  }
  return {};
}

/*
 * Returns Realpath(path) if successful, or the absolute path of "path"
 *
 * If emulating absolute path fails, "path" is returned as is.
 */
std::string RealpathOrSelf(const std::string& path) {
  std::string output;
  if (android::base::Realpath(path, &output)) {
    return output;
  }
  struct InputPathForm input_form {
    .path_to_convert = path, .follow_symlink = true,
  };
  auto absolute_path = EmulateAbsolutePath(input_form);
  return absolute_path.ok() ? *absolute_path : path;
}

}  // namespace

Result<void> CopyDirectoryRecursively(
    const std::string& src_dir_path, const std::string& dest_dir_path,
    const bool verify_dest_dir_empty,
    std::function<bool(const std::string&)> predicate) {
  CF_EXPECTF(FileExists(src_dir_path),
             "A file/directory \"{}\" does not exist.", src_dir_path);
  CF_EXPECTF(DirectoryExists(src_dir_path), "\"{}\" is not a directory.",
             src_dir_path);
  if (verify_dest_dir_empty) {
    CF_EXPECTF(!FileExists(dest_dir_path, /* follow symlink */ false),
               "Delete the destination directory \"{}\" first", dest_dir_path);
  }

  std::string dest_final_target = RealpathOrSelf(dest_dir_path);
  std::string src_final_target = RealpathOrSelf(src_dir_path);
  if (dest_final_target == src_final_target) {
    LOG(DEBUG) << "\"" << src_dir_path << "\" and \"" << dest_dir_path
               << "\" are effectively the same.";
    return {};
  }

  LOG(INFO) << "Copy from \"" << src_final_target << "\" to \""
            << dest_final_target << "\"";

  /**
   * On taking snapshot, we should delete dest_dir first. On Restoring,
   * we don't delete the runtime directory, eventually. We could, however,
   * start with deleting it.
   */
  CF_EXPECT(CopyDirectoryImpl(src_final_target, dest_final_target, predicate));
  return {};
}

Result<std::string> InstanceGuestSnapshotPath(const Json::Value& meta_json,
                                              const std::string& instance_id) {
  CF_EXPECTF(meta_json.isMember(kSnapshotPathField),
             "The given json is missing : {}", kSnapshotPathField);
  const std::string snapshot_path = meta_json[kSnapshotPathField].asString();

  const std::vector<std::string> guest_snapshot_path_selectors{
      kGuestSnapshotField, instance_id};
  const auto guest_snapshot_dir = CF_EXPECTF(
      GetValue<std::string>(meta_json, guest_snapshot_path_selectors),
      "root[\"{}\"][\"{}\"] is missing in \"{}\"", kGuestSnapshotField,
      instance_id, kMetaInfoJsonFileName);
  auto snapshot_path_direct_parent = snapshot_path + "/" + guest_snapshot_dir;
  LOG(DEBUG) << "Returning snapshot path : " << snapshot_path_direct_parent;
  return snapshot_path_direct_parent;
}

Result<Json::Value> CreateMetaInfo(const CuttlefishConfig& cuttlefish_config,
                                   const std::string& snapshot_path) {
  Json::Value meta_info;
  meta_info[kSnapshotPathField] = snapshot_path;

  const auto cuttlefish_home = StringFromEnv("HOME", "");
  CF_EXPECT(!cuttlefish_home.empty(),
            "\"HOME\" environment variable must be set.");
  meta_info[kCfHomeField] = cuttlefish_home;

  const auto instances = cuttlefish_config.Instances();
  // "id" -> relative path of instance_dir from cuttlefish_home
  //         + kGuestSnapshotField
  // e.g. "2" -> cuttlefish/instances/cvd-2/guest_snapshot
  std::unordered_map<std::string, std::string>
      id_to_relative_guest_snapshot_dir;
  for (const auto& instance : instances) {
    const std::string instance_snapshot_dir =
        instance.instance_dir() + "/" + kGuestSnapshotField;
    std::string_view sv_relative_path(instance_snapshot_dir);

    CF_EXPECTF(android::base::ConsumePrefix(&sv_relative_path, cuttlefish_home),
               "Instance Guest Snapshot Directory \"{}\""
               "is not a subdirectory of \"{}\"",
               instance_snapshot_dir, cuttlefish_home);
    if (!sv_relative_path.empty() && sv_relative_path.at(0) == '/') {
      sv_relative_path.remove_prefix(1);
    }
    id_to_relative_guest_snapshot_dir[instance.id()] =
        std::string(sv_relative_path);
  }

  Json::Value snapshot_mapping;
  // 2 -> cuttlefish/instances/cvd-2
  // relative path to cuttlefish_home
  for (const auto& [id_str, relative_guest_snapshot_dir] :
       id_to_relative_guest_snapshot_dir) {
    snapshot_mapping[id_str] = relative_guest_snapshot_dir;
  }
  meta_info[kGuestSnapshotField] = snapshot_mapping;
  return meta_info;
}

std::string SnapshotMetaJsonPath(const std::string& snapshot_path) {
  return snapshot_path + "/" + kMetaInfoJsonFileName;
}

Result<Json::Value> LoadMetaJson(const std::string& snapshot_path) {
  auto meta_json_path = SnapshotMetaJsonPath(snapshot_path);
  auto meta_json = CF_EXPECT(LoadFromFile(meta_json_path));
  return meta_json;
}

Result<std::vector<std::string>> GuestSnapshotDirectories(
    const std::string& snapshot_path) {
  auto meta_json = CF_EXPECT(LoadMetaJson(snapshot_path));
  CF_EXPECT(meta_json.isMember(kGuestSnapshotField));
  const auto& guest_snapshot_dir_jsons = meta_json[kGuestSnapshotField];
  std::vector<std::string> id_strs = guest_snapshot_dir_jsons.getMemberNames();
  std::vector<std::string> guest_snapshot_paths;
  for (const auto& id_str : id_strs) {
    CF_EXPECT(guest_snapshot_dir_jsons.isMember(id_str));
    std::string path_suffix = guest_snapshot_dir_jsons[id_str].asString();
    guest_snapshot_paths.push_back(snapshot_path + "/" + path_suffix);
  }
  return guest_snapshot_paths;
}

}  // namespace cuttlefish

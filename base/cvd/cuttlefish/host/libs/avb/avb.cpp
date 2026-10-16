/*
 * Copyright 2023 The Android Open Source Project
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
 *
 */

#include "cuttlefish/host/libs/avb/avb.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "android-base/unique_fd.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/files/copy.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/rename.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/posix/temp_failure_retry.h"  // IWYU pragma: keep
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kAddHashFooter[] = "add_hash_footer";
constexpr char kDefaultAlgorithm[] = "SHA256_RSA4096";
constexpr char kMakeVbmetaImage[] = "make_vbmeta_image";
// Taken from external/avb/libavb/avb_slot_verify.c; this define is not in the
// headers
constexpr size_t kVbMetaMaxSize = 65536ul;

// Writes the contents of `path`, zero padded to `kVbMetaMaxSize`, to the file
// `tmp_path` opened as `tmp_fd`, with the permission bits of `mode`, and
// flushes it to disk so it can be renamed over `path`.
Result<void> WritePaddedCopy(const std::string& path,
                             const std::string& tmp_path, const mode_t mode,
                             Fd& tmp_fd) {
  CF_EXPECT(Copy(path, tmp_path));
  CF_EXPECTF(tmp_fd.Chmod(mode & 07777), "chmod on '{}' failed: {}", tmp_path,
             tmp_fd.StrError());
  CF_EXPECTF(tmp_fd.Truncate(kVbMetaMaxSize), "Truncating '{}' failed",
             tmp_path);
  CF_EXPECTF(tmp_fd.Fsync(), "fsync on '{}' failed", tmp_path);
  return {};
}

// Pads `path` by renaming a padded copy over it, so other hard links to the
// original file keep their contents and a symlink at `path` is replaced rather
// than followed.
Result<void> PadByCopy(const std::string& path, const mode_t mode) {
  auto [tmp_fd, tmp_path] = CF_EXPECT(Fd::Mkostemp(path + "."));
  if (Result<void> res = WritePaddedCopy(path, tmp_path, mode, tmp_fd);
      !res.has_value()) {
    (void)RemoveFile(tmp_path);
    CF_EXPECTF(std::move(res), "Failed to pad a copy of '{}'", path);
  }
  if (Result<void, int> res = Rename(tmp_path, path); !res.has_value()) {
    (void)RemoveFile(tmp_path);
    CF_EXPECTF(std::move(res), "Failed to rename '{}' to '{}'", tmp_path, path);
  }
  return {};
}

}  // namespace

Avb::Avb() : Avb(AvbToolBinary(), kDefaultAlgorithm, TestKeyRsa4096()) {}

Avb::Avb(std::string avbtool_path)
    : Avb(std::move(avbtool_path), kDefaultAlgorithm, TestKeyRsa4096()) {}

Avb::Avb(std::string avbtool_path, std::string algorithm, std::string key)
    : avbtool_path_(std::move(avbtool_path)),
      algorithm_(std::move(algorithm)),
      key_(std::move(key)) {}

Command Avb::GenerateAddHashFooter(const std::string& image_path,
                                   const std::string& partition_name,
                                   const off_t partition_size_bytes) const {
  Command command(avbtool_path_);
  command.AddParameter(kAddHashFooter);
  if (!algorithm_.empty()) {
    command.AddParameter("--algorithm");
    command.AddParameter(algorithm_);
  }
  if (!key_.empty()) {
    command.AddParameter("--key");
    command.AddParameter(key_);
  }
  command.AddParameter("--image");
  command.AddParameter(image_path);
  command.AddParameter("--partition_name");
  command.AddParameter(partition_name);
  if (partition_size_bytes > 0) {
    command.AddParameter("--partition_size");
    command.AddParameter(partition_size_bytes);
  } else {
    command.AddParameter("--dynamic_partition_size");
  }
  return command;
}

Result<void> Avb::AddHashFooter(const std::string& image_path,
                                const std::string& partition_name,
                                const off_t partition_size_bytes) const {
  auto command =
      GenerateAddHashFooter(image_path, partition_name, partition_size_bytes);
  int exit_code = command.Start().Wait();
  CF_EXPECTF(exit_code == 0, "Failure running {} {}. Exited with status {}",
             command.Executable(), kAddHashFooter, exit_code);
  return {};
}

Command Avb::GenerateMakeVbMetaImage(
    const std::string& output_path,
    const std::vector<ChainPartition>& chained_partitions,
    const std::vector<std::string>& included_partitions,
    const std::vector<std::string>& extra_arguments) {
  Command command(avbtool_path_);
  command.AddParameter(kMakeVbmetaImage);
  command.AddParameter("--algorithm");
  command.AddParameter(algorithm_);
  command.AddParameter("--key");
  command.AddParameter(key_);
  command.AddParameter("--output");
  command.AddParameter(output_path);

  for (const auto& partition : chained_partitions) {
    const std::string argument = partition.name + ":" +
                                 partition.rollback_index + ":" +
                                 partition.key_path;
    command.AddParameter("--chain_partition");
    command.AddParameter(argument);
  }
  for (const auto& partition : included_partitions) {
    command.AddParameter("--include_descriptors_from_image");
    command.AddParameter(partition);
  }
  for (const auto& extra_arg : extra_arguments) {
    command.AddParameter(extra_arg);
  }
  return command;
}

Result<void> Avb::MakeVbMetaImage(
    const std::string& output_path,
    const std::vector<ChainPartition>& chained_partitions,
    const std::vector<std::string>& included_partitions,
    const std::vector<std::string>& extra_arguments) {
  auto command = GenerateMakeVbMetaImage(output_path, chained_partitions,
                                         included_partitions, extra_arguments);
  int exit_code = command.Start().Wait();
  CF_EXPECTF(exit_code == 0, "Failure running {} {}. Exited with status {}",
             command.Executable(), kMakeVbmetaImage, exit_code);
  CF_EXPECT(EnforceVbMetaSize(output_path));
  return {};
}

Result<void> EnforceVbMetaSize(const std::string& path) {
  struct stat link_stat{};
  CF_EXPECTF(lstat(path.c_str(), &link_stat) == 0, "lstat('{}') failed: {}",
             path, StrError(errno));
  if (!S_ISREG(link_stat.st_mode)) {
    // A symlink or other special file may refer to a file that is shared, so
    // pad a copy of its contents rather than writing through it.
    const struct stat target_stat = CF_EXPECT(Stat(path));
    CF_EXPECT_LE(target_stat.st_size, kVbMetaMaxSize);
    if (target_stat.st_size != kVbMetaMaxSize) {
      CF_EXPECT(PadByCopy(path, target_stat.st_mode));
    }
    return {};
  }
  CF_EXPECT_LE(link_stat.st_size, kVbMetaMaxSize);
  if (link_stat.st_size == kVbMetaMaxSize) {
    return {};
  }

  const int raw_fd =
      TEMP_FAILURE_RETRY(open(path.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC));
  if (raw_fd < 0) {
    const int open_errno = errno;
    // EACCES: the file is not writable. ELOOP: `path` became a symlink after
    // the lstat. Neither can be padded in place, but a copy can.
    CF_EXPECTF(open_errno == EACCES || open_errno == ELOOP,
               "open('{}') failed: {}", path, StrError(open_errno));
    CF_EXPECT(PadByCopy(path, link_stat.st_mode));
    return {};
  }
  android::base::unique_fd vbmeta_fd(raw_fd);

  // The link count check and the truncation both go through `vbmeta_fd`, so
  // they apply to the same file even if `path` is replaced in between. A hard
  // link created between the fstat and the ftruncate would be missed, but
  // nothing in the fetch or assemble flows creates links at that point.
  struct stat fd_stat{};
  CF_EXPECTF(fstat(vbmeta_fd.get(), &fd_stat) == 0, "fstat('{}') failed: {}",
             path, StrError(errno));
  if (fd_stat.st_nlink > 1) {
    // Other links share this file, so it must not be modified in place.
    CF_EXPECT(PadByCopy(path, fd_stat.st_mode));
    return {};
  }
  // Growing the file only extends it with a hole; no data is written.
  CF_EXPECTF(
      TEMP_FAILURE_RETRY(ftruncate(vbmeta_fd.get(), kVbMetaMaxSize)) == 0,
      "Truncating '{}' failed: {}", path, StrError(errno));
  CF_EXPECTF(fsync(vbmeta_fd.get()) == 0, "fsync on '{}' failed: {}", path,
             StrError(errno));
  return {};
}

}  // namespace cuttlefish

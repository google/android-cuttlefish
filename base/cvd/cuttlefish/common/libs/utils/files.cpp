/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/files.h"

#ifdef __linux__
#include <linux/fs.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "android-base/file.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/users.h"
#include "cuttlefish/files/copy.h"
#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/files/directory_exists.h"
#include "cuttlefish/files/file_device_id.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/files/link_or_copy.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/posix/realpath.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/posix/rename.h"
#include "cuttlefish/posix/stat.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

#ifdef __APPLE__
#define off64_t off_t
#define ftruncate64 ftruncate
#endif

namespace cuttlefish {

Result<bool> CanHardLink(const std::string& source,
                         const std::string& destination) {
  return CF_EXPECT(FileDeviceId(source)) ==
         CF_EXPECT(FileDeviceId(destination));
}

bool FileHasContent(const std::string& path) { return FileSize(path) > 0; }

Result<void> LinkOrCopyDirectoryContentsRecursively(
    const std::string& source, const std::string& destination) {
  CF_EXPECTF(DirectoryExists(source), "Source '{}' is not a directory", source);

  CF_EXPECT(EnsureDirectoryExists(destination, 0755));

  auto linker_or_copier =
      [&source,
       &destination](const std::string& filepath) mutable -> Result<void> {
    const std::string src_path = filepath;
    const std::string dst_path =
        destination + "/" + filepath.substr(source.size() + 1);
    if (DirectoryExists(src_path)) {
      CF_EXPECT(EnsureDirectoryExists(dst_path));
      return {};
    }
    const bool overwrite_existing = true;
    CF_EXPECT(LinkOrCopy(src_path, dst_path, overwrite_existing));
    return {};
  };
  CF_EXPECT(WalkDirectory(source, linker_or_copier));

  return {};
}

Result<void> MoveDirectoryContents(const std::string& source,
                                   const std::string& destination) {
  CF_EXPECTF(DirectoryExists(source), "Source '{}' is not a directory", source);
  CF_EXPECT(EnsureDirectoryExists(destination));

  bool should_rename = CF_EXPECT(CanRename(source, destination));
  std::vector<std::string> contents = CF_EXPECT(DirectoryContents(source));
  for (const std::string& filepath : contents) {
    std::string src_filepath = source + "/" + filepath;
    std::string dst_filepath = destination + "/" + filepath;
    if (should_rename) {
      CF_EXPECT(Rename(src_filepath, dst_filepath));
    } else {
      CF_EXPECT(
          Copy(src_filepath, dst_filepath),
          "copy " << src_filepath << " to " << dst_filepath << " failed.");
    }
  }

  return {};
}

Result<std::vector<std::string>> DirectoryContentsPaths(
    const std::string& path) {
  std::vector<std::string> result = CF_EXPECT(DirectoryContents(path));
  for (std::string& filename : result) {
    filename = fmt::format("{}/{}", path, filename);
  }
  return result;
}

Result<void> EnsureDirectoryExists(const std::string& directory_path,
                                   const mode_t mode,
                                   const std::string& group_name) {
  if (DirectoryExists(directory_path, /* follow_symlinks */ true)) {
    return {};
  }
  if (FileExists(directory_path, false) && !FileExists(directory_path, true)) {
    // directory_path is a link to a path that doesn't exist. This could happen
    // after executing certain cvd subcommands.
    CF_EXPECT(RemoveFile(directory_path),
              "Can't remove broken link: " << directory_path);
  }
  const auto parent_dir = android::base::Dirname(directory_path);
  if (parent_dir.size() > 1) {
    CF_EXPECT(EnsureDirectoryExists(parent_dir, mode, group_name));
  }
  VLOG(1) << "Setting up " << directory_path;
  if (mkdir(directory_path.c_str(), mode) < 0 && errno != EEXIST) {
    return CF_ERRNO("Failed to create directory: \"" << directory_path << "\""
                                                     << strerror(errno));
  }
  // TODO(schuffelen): Find an alternative for host-sandboxing mode
  if (InSandbox()) {
    return {};
  }

  CF_EXPECTF(chmod(directory_path.c_str(), mode) == 0,
             "Failed to set permission on {}: {}", directory_path,
             strerror(errno));

  if (!group_name.empty()) {
    CF_EXPECT(ChangeGroup(directory_path, group_name));
  }

  return {};
}

Result<void> ChangeGroup(const std::string& path,
                         const std::string& group_name) {
  auto groupId = GroupIdFromName(group_name);

  if (groupId == -1) {
    return CF_ERRF("Failed to get group id: {}", group_name);
  }

  if (chown(path.c_str(), -1, groupId) != 0) {
    return CF_ERRNO("Failed to set group for path: "
                    << path << ", " << group_name << ", " << strerror(errno));
  }

  return {};
}

bool CanAccess(const std::string& path, const int mode) {
  return access(path.c_str(), mode) == 0;
}

std::string AbsolutePath(std::string_view path) {
  if (path.empty()) {
    return {};
  }
  if (path[0] == '/') {
    return std::string(path);
  }

  Result<std::string> real_cwd = RealPath(".");
  if (!real_cwd.has_value()) {
    LOG(WARNING) << "Could not get real path for current directory \".\": "
                 << real_cwd.error();
    return {};
  }
  return absl::StrCat(*real_cwd, "/", path);
}

off_t FileSize(const std::string& path) {
  static auto get_size = [](const struct stat& st) { return st.st_size; };
  return Stat(path).transform(get_size).value_or(0);
}

Result<uid_t> FileOwner(const std::string& path) {
  return CF_EXPECT(Stat(path)).st_uid;
}

bool MakeFileExecutable(const std::string& path) {
  VLOG(0) << "Making " << path << " executable";
  return chmod(path.c_str(), S_IRWXU) == 0;
}

Result<std::chrono::system_clock::time_point> FileModificationTime(
    const std::string& path) {
  struct stat st = CF_EXPECT(Stat(path));
#ifdef __linux__
  std::chrono::seconds seconds(st.st_mtim.tv_sec);
#elif defined(__APPLE__)
  std::chrono::seconds seconds(st.st_mtimespec.tv_sec);
#else
#error "Unsupported operating system"
#endif
  return std::chrono::system_clock::time_point(seconds);
}

Result<std::string> RenameFile(const std::string& current_filepath,
                               const std::string& target_filepath) {
  if (current_filepath != target_filepath) {
    CF_EXPECT(Rename(current_filepath, target_filepath));
  }
  return target_filepath;
}

std::string ReadFile(const std::string& file) {
  std::string contents;
  std::ifstream in(file, std::ios::in | std::ios::binary);
  in.seekg(0, std::ios::end);
  if (in.fail()) {
    // TODO(schuffelen): Return a failing Result instead
    return "";
  }
  if (in.tellg() == std::ifstream::pos_type(-1)) {
    PLOG(ERROR) << "Failed to seek on " << file;
    return "";
  }
  contents.resize(in.tellg());
  in.seekg(0, std::ios::beg);
  if (!contents.empty()) {
    in.read(&contents[0], contents.size());
  }
  in.close();
  return (contents);
}

Result<std::string> ReadFileContents(const std::string& path) {
  CF_EXPECTF(FileExists(path), "The file at '{}' does not exist.", path);
  Fd file = CF_EXPECT(Fd::Open(path, O_RDONLY));
  return CF_EXPECT(ReadToString(file));
}
Result<void> WriteNewFile(const std::string& filepath, std::string_view content,
                          mode_t mode) {
  SharedFD file_fd =
      CF_EXPECT(Fd::Open(filepath, O_CREAT | O_EXCL | O_WRONLY, mode));
  const auto written_size = WriteAll(file_fd, content);
  CF_EXPECTF(written_size == content.size(),
             "Failed to write all content to file. Error:\n",
             file_fd->StrError());
  return {};
}

std::string CurrentDirectory() {
  std::vector<char> process_wd(1 << 12, ' ');
  while (getcwd(process_wd.data(), process_wd.size()) == nullptr) {
    if (errno == ERANGE) {
      process_wd.resize(process_wd.size() * 2, ' ');
    } else {
      PLOG(ERROR) << "getcwd failed";
      return "";
    }
  }
  // Will find the null terminator and size the string appropriately.
  return std::string(process_wd.data());
}

FileSizes SparseFileSizes(const std::string& path) {
  SharedFD fd = Fd::Open(path, O_RDONLY).value_or(Fd());
  if (!fd->IsOpen()) {
    LOG(ERROR) << "Could not open \"" << path << "\": " << fd->StrError();
    return {};
  }
  off_t farthest_seek = fd->LSeek(0, SEEK_END);
  VLOG(1) << "Farthest seek: " << farthest_seek;
  if (farthest_seek == -1) {
    LOG(ERROR) << "Could not lseek in \"" << path << "\": " << fd->StrError();
    return {};
  }
  off_t data_bytes = 0;
  off_t offset = 0;
  while (offset < farthest_seek) {
    off_t new_offset = fd->LSeek(offset, SEEK_HOLE);
    if (new_offset == -1) {
      // ENXIO is returned when there are no more blocks of this type coming.
      if (fd->GetErrno() == ENXIO) {
        break;
      } else {
        LOG(ERROR) << "Could not lseek in \"" << path
                   << "\": " << fd->StrError();
        return {};
      }
    } else {
      data_bytes += new_offset - offset;
      offset = new_offset;
    }
    if (offset >= farthest_seek) {
      break;
    }
    new_offset = fd->LSeek(offset, SEEK_DATA);
    if (new_offset == -1) {
      // ENXIO is returned when there are no more blocks of this type coming.
      if (fd->GetErrno() == ENXIO) {
        break;
      } else {
        LOG(ERROR) << "Could not lseek in \"" << path
                   << "\": " << fd->StrError();
        return {};
      }
    } else {
      offset = new_offset;
    }
  }
  return (FileSizes){.sparse_size = farthest_seek, .disk_size = data_bytes};
}

Result<std::string> FindFile(const std::string& path,
                             const std::string& target_name) {
  std::string ret;
  auto callback = [&ret, &target_name](
                      const std::string& filename) mutable -> Result<void> {
    if (android::base::Basename(filename) == target_name) {
      ret = filename;
    }
    return {};
  };
  CF_EXPECT(WalkDirectory(path, callback));
  CF_EXPECTF(!ret.empty(), "No file matching '{}' found in '{}'", target_name,
             path);
  return ret;
}

// Recursively enumerate files in |dir|, and invoke the callback function with
// path to each file/directory.
Result<void> WalkDirectory(const std::string& dir,
                           const WalkDirectoryCallback& callback) {
  for (const std::string& filename : CF_EXPECT(DirectoryContents(dir))) {
    auto file_path = dir + "/";
    file_path.append(filename);
    CF_EXPECT(callback(file_path));
    if (DirectoryExists(file_path)) {
      CF_EXPECT(WalkDirectory(file_path, callback));
    }
  }
  return {};
}

std::vector<std::string> Path(const std::string& env_name) {
  // TODO: Assumes a SUS system. Elsewhere we may need to change the delimiter.
  // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html
  return absl::StrSplit(StringFromEnv(env_name).value_or(""), ':');
}

Result<std::string> Search(const std::vector<std::string>& path,
                           std::string_view name) {
  // TODO: Assumes a SUS system. Elsewhere we may need to change the delimiter.
  // https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap08.html
  for (const auto& dir : path) {
    std::string abs_path = absl::StrCat(dir, "/", name);
    if (FileExists(abs_path)) {
      return abs_path;
    }
  }
  return CF_ERRF("Not found: '{}', path '{}'", name, absl::StrJoin(path, ":"));
}

Result<SharedFD> CreateOrReuseAndDrainFifo(const std::string& path,
                                           mode_t mode) {
  Result<struct stat> st = Stat(path);
  if (st.has_value()) {
    CF_EXPECTF(S_ISFIFO(st->st_mode), "File at '{}' exists but is not a FIFO",
               path);
  } else {
    CF_EXPECTF(TEMP_FAILURE_RETRY(mkfifo(path.c_str(), mode)) == 0,
               "Failed to mkfifo('{}', {:o}): {}", path, mode, StrError(errno));
  }

  Fd ret = CF_EXPECT(Fd::Open(path, O_RDWR));

  if (st.has_value()) {
    int flags = ret.Fcntl(F_GETFL, 0);
    if (flags >= 0) {
      ret.Fcntl(F_SETFL, flags | O_NONBLOCK);
      char buf[4096];
      while (ret.Read(buf, sizeof(buf)).value_or(0) > 0) {
        // Reading while there is data to read
      }
      ret.Fcntl(F_SETFL, flags);
    }
  }

  return ret;
}

}  // namespace cuttlefish

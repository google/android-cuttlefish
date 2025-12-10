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
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/sendfile.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <memory>
#include <numeric>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/in_sandbox.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/users.h"

#ifdef __APPLE__
#define off64_t off_t
#define ftruncate64 ftruncate
#endif

namespace cuttlefish {

bool FileExists(const std::string& path, bool follow_symlinks) {
  struct stat st {};
  return (follow_symlinks ? stat : lstat)(path.c_str(), &st) == 0;
}

Result<dev_t> FileDeviceId(const std::string& path) {
  struct stat out;
  CF_EXPECTF(
      stat(path.c_str(), &out) == 0,
      "stat() failed trying to retrieve device ID information for \"{}\" "
      "with error: {}",
      path, strerror(errno));
  return out.st_dev;
}

Result<bool> CanHardLink(const std::string& source,
                         const std::string& destination) {
  return CF_EXPECT(FileDeviceId(source)) ==
         CF_EXPECT(FileDeviceId(destination));
}

Result<ino_t> FileInodeNumber(const std::string& path) {
  struct stat out;
  CF_EXPECTF(
      stat(path.c_str(), &out) == 0,
      "stat() failed trying to retrieve inode num information for \"{}\" "
      "with error: {}",
      path, strerror(errno));
  return out.st_ino;
}

Result<bool> AreHardLinked(const std::string& source,
                           const std::string& destination) {
  return (CF_EXPECT(FileDeviceId(source)) ==
          CF_EXPECT(FileDeviceId(destination))) &&
         (CF_EXPECT(FileInodeNumber(source)) ==
          CF_EXPECT(FileInodeNumber(destination)));
}

Result<std::string> CreateHardLink(const std::string& target,
                                   const std::string& hardlink,
                                   const bool overwrite_existing) {
  if (FileExists(hardlink)) {
    if (CF_EXPECT(AreHardLinked(target, hardlink))) {
      return hardlink;
    }
    if (!overwrite_existing) {
      return CF_ERRF(
          "Cannot hardlink from \"{}\" to \"{}\", the second file already "
          "exists and is not hardlinked to the first",
          target, hardlink);
    }
    LOG(WARNING) << "Overwriting existing file \"" << hardlink << "\" with \""
                 << target << "\" from the cache";
    CF_EXPECTF(unlink(hardlink.c_str()) == 0,
               "Failed to unlink \"{}\" with error: {}", hardlink,
               strerror(errno));
  }
  CF_EXPECTF(link(target.c_str(), hardlink.c_str()) == 0,
             "link() failed trying to create hardlink from \"{}\" to \"{}\" "
             "with error: {}",
             target, hardlink, strerror(errno));
  return hardlink;
}

bool FileHasContent(const std::string& path) {
  return FileSize(path) > 0;
}

Result<void> HardLinkDirecoryContentsRecursively(
    const std::string& source, const std::string& destination) {
  CF_EXPECTF(IsDirectory(source), "Source '{}' is not a directory", source);
  EnsureDirectoryExists(destination, 0755);

  const std::function<bool(const std::string&)> linker =
      [&source, &destination](const std::string& filepath) mutable {
        std::string src_path = filepath;
        std::string dst_path =
            destination + "/" + filepath.substr(source.size() + 1);
        if (IsDirectory(src_path)) {
          EnsureDirectoryExists(dst_path);
          return true;
        }
        bool overwrite_existing = true;
        Result<std::string> result =
            CreateHardLink(src_path, dst_path, overwrite_existing);
        return result.ok();
      };
  CF_EXPECT(WalkDirectory(source, linker));

  return {};
}

Result<void> MoveDirectoryContents(const std::string& source,
                                   const std::string& destination) {
  CF_EXPECTF(IsDirectory(source), "Source '{}' is not a directory", source);
  CF_EXPECT(EnsureDirectoryExists(destination));

  bool should_rename = CF_EXPECT(CanRename(source, destination));
  std::vector<std::string> contents = CF_EXPECT(DirectoryContents(source));
  for (const std::string& filepath : contents) {
    std::string src_filepath = source + "/" + filepath;
    std::string dst_filepath = destination + "/" + filepath;
    if (should_rename) {
      CF_EXPECT(rename(src_filepath.c_str(), dst_filepath.c_str()) == 0,
                "rename " << src_filepath << " to " << dst_filepath
                          << " failed: " << strerror(errno));
    } else {
      CF_EXPECT(
          Copy(src_filepath, dst_filepath),
          "copy " << src_filepath << " to " << dst_filepath << " failed.");
    }
  }

  return {};
}

Result<std::vector<std::string>> DirectoryContents(const std::string& path) {
  std::vector<std::string> ret;
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  CF_EXPECTF(dir != nullptr, "Could not read from dir \"{}\"", path);
  struct dirent* ent{};
  while ((ent = readdir(dir.get()))) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    ret.emplace_back(ent->d_name);
  }
  return ret;
}

Result<std::vector<std::string>> DirectoryContentsPaths(
    const std::string& path) {
  std::vector<std::string> result = CF_EXPECT(DirectoryContents(path));
  for (std::string& filename : result) {
    filename = fmt::format("{}/{}", path, filename);
  }
  return result;
}

bool DirectoryExists(const std::string& path, bool follow_symlinks) {
  struct stat st {};
  if ((follow_symlinks ? stat : lstat)(path.c_str(), &st) == -1) {
    return false;
  }
  if ((st.st_mode & S_IFMT) != S_IFDIR) {
    return false;
  }
  return true;
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
  LOG(VERBOSE) << "Setting up " << directory_path;
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
    return CF_ERR("Failed to get group id: ") << group_name;
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

Result<bool> IsDirectoryEmpty(const std::string& path) {
  std::unique_ptr<DIR, int (*)(DIR*)> direc(opendir(path.c_str()), closedir);
  CF_EXPECTF(direc.get(), "opendir('{}') failed: {}", path, StrError(errno));

  int cnt = 0;
  while (::readdir(direc.get())) {
    cnt++;
    if (cnt > 2) {
      return false;
    }
  }
  return true;
}

Result<void> RecursivelyRemoveDirectory(const std::string& path) {
  // Copied from libbase TemporaryDir destructor.
  auto callback = [](const char* child, const struct stat*, int file_type,
                     struct FTW*) -> int {
    switch (file_type) {
      case FTW_D:
      case FTW_DP:
      case FTW_DNR:
        if (rmdir(child) == -1) {
          PLOG(ERROR) << "rmdir " << child;
          return -1;
        }
        break;
      case FTW_NS:
      default:
        if (rmdir(child) != -1) {
          break;
        }
        // FALLTHRU (for gcc, lint, pcc, etc; and following for clang)
        FALLTHROUGH_INTENDED;
      case FTW_F:
      case FTW_SL:
      case FTW_SLN:
        if (unlink(child) == -1) {
          PLOG(ERROR) << "unlink " << child;
          return -1;
        }
        break;
    }
    return 0;
  };

  if (nftw(path.c_str(), callback, 128, FTW_DEPTH | FTW_PHYS) < 0) {
    return CF_ERRNO("Failed to remove directory \""
                    << path << "\": " << strerror(errno));
  }
  return {};
}

namespace {

bool SendFile(int out_fd, int in_fd, off64_t* offset, size_t count) {
  while (count > 0) {
#ifdef __linux__
    const auto bytes_written =
        TEMP_FAILURE_RETRY(sendfile(out_fd, in_fd, offset, count));
    if (bytes_written <= 0) {
      return false;
    }
#elif defined(__APPLE__)
    off_t bytes_written = count;
    auto success = TEMP_FAILURE_RETRY(
        sendfile(in_fd, out_fd, *offset, &bytes_written, nullptr, 0));
    *offset += bytes_written;
    if (success < 0 || bytes_written == 0) {
      return false;
    }
#endif
    count -= bytes_written;
  }
  return true;
}

}  // namespace

bool Copy(const std::string& from, const std::string& to) {
  android::base::unique_fd fd_from(
      open(from.c_str(), O_RDONLY | O_CLOEXEC));
  android::base::unique_fd fd_to(
      open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));

  if (fd_from.get() < 0 || fd_to.get() < 0) {
    return false;
  }

  off_t farthest_seek = lseek(fd_from.get(), 0, SEEK_END);
  if (farthest_seek == -1) {
    PLOG(ERROR) << "Could not lseek in \"" << from << "\"";
    return false;
  }
  if (ftruncate64(fd_to.get(), farthest_seek) < 0) {
    PLOG(ERROR) << "Failed to ftruncate " << to;
  }
  off_t offset = 0;
  while (offset < farthest_seek) {
    off_t new_offset = lseek(fd_from.get(), offset, SEEK_HOLE);
    if (new_offset == -1) {
      // ENXIO is returned when there are no more blocks of this type
      // coming.
      if (errno == ENXIO) {
        return true;
      }
      PLOG(ERROR) << "Could not lseek in \"" << from << "\"";
      return false;
    }
    auto data_bytes = new_offset - offset;
    if (lseek(fd_to.get(), offset, SEEK_SET) < 0) {
      PLOG(ERROR) << "lseek() on " << to << " failed";
      return false;
    }
    if (!SendFile(fd_to.get(), fd_from.get(), &offset, data_bytes)) {
      PLOG(ERROR) << "sendfile() failed";
      return false;
    }
    CHECK_EQ(offset, new_offset);
    if (offset >= farthest_seek) {
      return true;
    }
    new_offset = lseek(fd_from.get(), offset, SEEK_DATA);
    if (new_offset == -1) {
      // ENXIO is returned when there are no more blocks of this type
      // coming.
      if (errno == ENXIO) {
        return true;
      }
      PLOG(ERROR) << "Could not lseek in \"" << from << "\"";
      return false;
    }
    offset = new_offset;
  }
  return true;
}

std::string AbsolutePath(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  if (path[0] == '/') {
    return path;
  }
  if (path[0] == '~') {
    LOG(WARNING) << "Tilde expansion in path " << path <<" is not supported";
    return {};
  }

  std::array<char, PATH_MAX> buffer{};
  if (!realpath(".", buffer.data())) {
    LOG(WARNING) << "Could not get real path for current directory \".\""
                 << ": " << strerror(errno);
    return {};
  }
  return std::string{buffer.data()} + "/" + path;
}

off_t FileSize(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) == -1) {
    return 0;
  }
  return st.st_size;
}

bool MakeFileExecutable(const std::string& path) {
  LOG(DEBUG) << "Making " << path << " executable";
  return chmod(path.c_str(), S_IRWXU) == 0;
}

Result<std::chrono::system_clock::time_point> FileModificationTime(
    const std::string& path) {
  struct stat st;
  CF_EXPECTF(stat(path.c_str(), &st) == 0,
             "stat() failed retrieving file modification time on \"{}\" with "
             "error: {}",
             path, strerror(errno));
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
    CF_EXPECT(rename(current_filepath.c_str(), target_filepath.c_str()) == 0,
              "rename " << current_filepath << " to " << target_filepath
                        << " failed: " << strerror(errno));
  }
  return target_filepath;
}

bool RemoveFile(const std::string& file) {
  LOG(DEBUG) << "Removing file " << file;
  if (remove(file.c_str()) == 0) {
    return true;
  }
  LOG(ERROR) << "Failed to remove file " << file << " : "
             << std::strerror(errno);
  return false;
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
  in.read(&contents[0], contents.size());
  in.close();
  return(contents);
}

Result<std::string> ReadFileContents(const std::string& filepath) {
  CF_EXPECTF(FileExists(filepath), "The file at \"{}\" does not exist.",
             filepath);
  auto file = SharedFD::Open(filepath, O_RDONLY);
  CF_EXPECTF(file->IsOpen(), "Failed to open file \"{}\".  Error: {}\n",
             filepath, file->StrError());
  std::string file_content;
  auto size = ReadAll(file, &file_content);
  CF_EXPECTF(size >= 0, "Failed to read file contents.  Error: {}\n",
             file->StrError());
  return file_content;
}

Result<void> WriteNewFile(const std::string& filepath, std::string_view content,
                          mode_t mode) {
  CF_EXPECTF(!FileExists(filepath), "File already exists: {}", filepath);
  SharedFD file_fd = SharedFD::Open(filepath, O_CREAT | O_WRONLY, mode);
  CF_EXPECTF(file_fd->IsOpen(), "Failed to open file \"{}\" for writing: {}",
             filepath, file_fd->StrError());
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
  auto fd = SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen()) {
    LOG(ERROR) << "Could not open \"" << path << "\": " << fd->StrError();
    return {};
  }
  off_t farthest_seek = fd->LSeek(0, SEEK_END);
  LOG(VERBOSE) << "Farthest seek: " << farthest_seek;
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
        LOG(ERROR) << "Could not lseek in \"" << path << "\": " << fd->StrError();
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
        LOG(ERROR) << "Could not lseek in \"" << path << "\": " << fd->StrError();
        return {};
      }
    } else {
      offset = new_offset;
    }
  }
  return (FileSizes) { .sparse_size = farthest_seek, .disk_size = data_bytes };
}

bool FileIsSocket(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

/**
 * Find an image file through the input path and pattern.
 *
 * If it finds the file, return the path string.
 * If it can't find the file, return empty string.
 */
std::string FindImage(const std::string& search_path,
                      const std::vector<std::string>& pattern) {
  const std::string& search_path_extend = search_path + "/";
  for (const auto& name : pattern) {
    std::string image = search_path_extend + name;
    if (FileExists(image)) {
      return image;
    }
  }
  return "";
}

Result<std::string> FindFile(const std::string& path,
                             const std::string& target_name) {
  std::string ret;
  auto res = WalkDirectory(
      path, [&ret, &target_name](const std::string& filename) mutable {
        if (android::base::Basename(filename) == target_name) {
          ret = filename;
        }
        return true;
      });
  if (!res.ok()) {
    return "";
  }
  return ret;
}

// Recursively enumerate files in |dir|, and invoke the callback function with
// path to each file/directory.
Result<void> WalkDirectory(
    const std::string& dir,
    const std::function<bool(const std::string&)>& callback) {
  const auto files = CF_EXPECT(DirectoryContents(dir));
  for (const auto& filename : files) {
    auto file_path = dir + "/";
    file_path.append(filename);
    callback(file_path);
    if (DirectoryExists(file_path)) {
      auto res = WalkDirectory(file_path, callback);
      if (!res.ok()) {
        return res;
      }
    }
  }
  return {};
}

namespace {

std::vector<std::string> FoldPath(std::vector<std::string> elements,
                                  std::string token) {
  static constexpr std::array kIgnored = {".", "..", ""};
  if (token == ".." && !elements.empty()) {
    elements.pop_back();
  } else if (!Contains(kIgnored, token)) {
    elements.emplace_back(token);
  }
  return elements;
}

Result<std::vector<std::string>> CalculatePrefix(
    const InputPathForm& path_info) {
  const auto& path = path_info.path_to_convert;
  std::string working_dir;
  if (path_info.current_working_dir) {
    working_dir = *path_info.current_working_dir;
  } else {
    working_dir = CurrentDirectory();
  }
  std::vector<std::string> prefix;
  if (path == "~" || absl::StartsWith(path, "~/")) {
    const auto home_dir =
        path_info.home_dir.value_or(CF_EXPECT(SystemWideUserHome()));
    prefix = android::base::Tokenize(home_dir, "/");
  } else if (!absl::StartsWith(path, "/")) {
    prefix = android::base::Tokenize(working_dir, "/");
  }
  return prefix;
}

}  // namespace

Result<std::string> EmulateAbsolutePath(const InputPathForm& path_info) {
  const auto& path = path_info.path_to_convert;
  std::string working_dir;
  if (path_info.current_working_dir) {
    working_dir = *path_info.current_working_dir;
  } else {
    working_dir = CurrentDirectory();
  }
  CF_EXPECT(absl::StartsWith(working_dir, "/"),
            "Current working directory should be given in an absolute path.");

  if (path.empty()) {
    LOG(ERROR) << "The requested path to convert an absolute path is empty.";
    return "";
  }

  auto prefix = CF_EXPECT(CalculatePrefix(path_info));
  std::vector<std::string> components;
  components.insert(components.end(), prefix.begin(), prefix.end());
  auto tokens = android::base::Tokenize(path, "/");
  // remove first ~
  if (!tokens.empty() && tokens.at(0) == "~") {
    tokens.erase(tokens.begin());
  }
  components.insert(components.end(), tokens.begin(), tokens.end());

  std::string combined = android::base::Join(components, "/");
  CF_EXPECTF(!Contains(components, "~"),
             "~ is not allowed in the middle of the path: {}", combined);

  auto processed_tokens = std::accumulate(components.begin(), components.end(),
                                          std::vector<std::string>{}, FoldPath);

  const auto processed_path = "/" + android::base::Join(processed_tokens, "/");

  std::string real_path = processed_path;
  if (path_info.follow_symlink && FileExists(processed_path)) {
    CF_EXPECTF(android::base::Realpath(processed_path, &real_path),
               "Failed to effectively conduct readpath -f {}", processed_path);
  }
  return real_path;
}

}  // namespace cuttlefish

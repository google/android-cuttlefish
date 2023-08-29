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

#include "common/libs/utils/files.h"

#ifdef __linux__
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <istream>
#include <memory>
#include <numeric>
#include <ostream>
#include <ratio>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/inotify.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "common/libs/utils/users.h"

#ifdef __APPLE__
#define off64_t off_t
#define ftruncate64 ftruncate
#endif

namespace cuttlefish {

bool FileExists(const std::string& path, bool follow_symlinks) {
  struct stat st {};
  return (follow_symlinks ? stat : lstat)(path.c_str(), &st) == 0;
}

bool FileHasContent(const std::string& path) {
  return FileSize(path) > 0;
}

Result<std::vector<std::string>> DirectoryContents(const std::string& path) {
  std::vector<std::string> ret;
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  CF_EXPECTF(dir != nullptr, "Could not read from dir \"{}\"", path);
  struct dirent* ent{};
  while ((ent = readdir(dir.get()))) {
    ret.emplace_back(ent->d_name);
  }
  return ret;
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
  const auto parent_dir = android::base::Dirname(directory_path);
  if (parent_dir.size() > 1) {
    EnsureDirectoryExists(parent_dir, mode, group_name);
  }
  LOG(VERBOSE) << "Setting up " << directory_path;
  if (mkdir(directory_path.c_str(), mode) < 0 && errno != EEXIST) {
    return CF_ERRNO("Failed to create directory: \"" << directory_path << "\""
                                                     << strerror(errno));
  }

  if (group_name != "") {
    ChangeGroup(directory_path, group_name);
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
    return CF_ERRNO("Feailed to set group for path: "
                    << path << ", " << group_name << ", " << strerror(errno));
  }

  return {};
}

bool CanAccess(const std::string& path, const int mode) {
  return access(path.c_str(), mode) == 0;
}

bool IsDirectoryEmpty(const std::string& path) {
  auto direc = ::opendir(path.c_str());
  if (!direc) {
    LOG(ERROR) << "IsDirectoryEmpty test failed with " << path
               << " as it failed to be open" << std::endl;
    return false;
  }

  decltype(::readdir(direc)) sub = nullptr;
  int cnt {0};
  while ( (sub = ::readdir(direc)) ) {
    cnt++;
    if (cnt > 2) {
    LOG(ERROR) << "IsDirectoryEmpty test failed with " << path
               << " as it exists but not empty" << std::endl;
      return false;
    }
  }
  return true;
}

bool RecursivelyRemoveDirectory(const std::string& path) {
  // Copied from libbase TemporaryDir destructor.
  auto callback = [](const char* child, const struct stat*, int file_type,
                     struct FTW*) -> int {
    switch (file_type) {
      case FTW_D:
      case FTW_DP:
      case FTW_DNR:
        if (rmdir(child) == -1) {
          PLOG(ERROR) << "rmdir " << child;
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
        }
        break;
    }
    return 0;
  };

  return nftw(path.c_str(), callback, 128, FTW_DEPTH | FTW_MOUNT | FTW_PHYS) ==
         0;
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

// TODO(schuffelen): Use std::filesystem::last_write_time when on C++17
std::chrono::system_clock::time_point FileModificationTime(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) == -1) {
    return std::chrono::system_clock::time_point();
  }
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
  return remove(file.c_str()) == 0;
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

std::string CurrentDirectory() {
  std::unique_ptr<char, void (*)(void*)> cwd(getcwd(nullptr, 0), &free);
  std::string process_cwd(cwd.get());
  if (!cwd) {
    PLOG(ERROR) << "`getcwd(nullptr, 0)` failed";
    return "";
  }
  return process_cwd;
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

std::string cpp_basename(const std::string& str) {
  char* copy = strdup(str.c_str()); // basename may modify its argument
  std::string ret(basename(copy));
  free(copy);
  return ret;
}

std::string cpp_dirname(const std::string& str) {
  return android::base::Dirname(str);
}

bool FileIsSocket(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

int GetDiskUsage(const std::string& path) {
  Command du_cmd("du");
  du_cmd.AddParameter("-b");
  du_cmd.AddParameter("-k");
  du_cmd.AddParameter("-s");
  du_cmd.AddParameter(path);
  SharedFD read_fd;
  SharedFD write_fd;
  SharedFD::Pipe(&read_fd, &write_fd);
  du_cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, write_fd);
  auto subprocess = du_cmd.Start();
  std::array<char, 1024> text_output{};
  const auto bytes_read = read_fd->Read(text_output.data(), text_output.size());
  CHECK_GT(bytes_read, 0) << "Failed to read from pipe " << strerror(errno);
  std::move(subprocess).Wait();
  return atoi(text_output.data()) * 1024;
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

std::string FindFile(const std::string& path, const std::string& target_name) {
  std::string ret;
  WalkDirectory(path,
                [&ret, &target_name](const std::string& filename) mutable {
                  if (cpp_basename(filename) == target_name) {
                    ret = filename;
                  }
                  return true;
                });
  return ret;
}

// Recursively enumerate files in |dir|, and invoke the callback function with
// path to each file/directory.
Result<void> WalkDirectory(
    const std::string& dir,
    const std::function<bool(const std::string&)>& callback) {
  const auto files = CF_EXPECT(DirectoryContents(dir));
  for (const auto& filename : files) {
    if (filename == "." || filename == "..") {
      continue;
    }
    auto file_path = dir + "/";
    file_path.append(filename);
    callback(file_path);
    if (DirectoryExists(file_path)) {
      WalkDirectory(file_path, callback);
    }
  }
  return {};
}

#ifdef __linux__
class InotifyWatcher {
 public:
  InotifyWatcher(int inotify, const std::string& path, int watch_mode)
      : inotify_(inotify) {
    watch_ = inotify_add_watch(inotify_, path.c_str(), watch_mode);
  }
  virtual ~InotifyWatcher() { inotify_rm_watch(inotify_, watch_); }

 private:
  int inotify_;
  int watch_;
};

static Result<void> WaitForFileInternal(const std::string& path, int timeoutSec,
                                        int inotify) {
  CF_EXPECT_NE(path, "", "Path is empty");

  if (FileExists(path, true)) {
    return {};
  }

  const auto targetTime =
      std::chrono::system_clock::now() + std::chrono::seconds(timeoutSec);

  const auto parentPath = cpp_dirname(path);
  const auto filename = cpp_basename(path);

  CF_EXPECT(WaitForFile(parentPath, timeoutSec),
            "Error while waiting for parent directory creation");

  auto watcher = InotifyWatcher(inotify, parentPath.c_str(), IN_CREATE);

  if (FileExists(path, true)) {
    return {};
  }

  while (true) {
    const auto currentTime = std::chrono::system_clock::now();

    if (currentTime >= targetTime) {
      return CF_ERR("Timed out");
    }

    const auto timeRemain =
        std::chrono::duration_cast<std::chrono::microseconds>(targetTime -
                                                              currentTime)
            .count();
    const auto secondInUsec =
        std::chrono::microseconds(std::chrono::seconds(1)).count();
    struct timeval timeout;

    timeout.tv_sec = timeRemain / secondInUsec;
    timeout.tv_usec = timeRemain % secondInUsec;

    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(inotify, &readfds);

    auto ret = select(inotify + 1, &readfds, NULL, NULL, &timeout);

    if (ret == 0) {
      return CF_ERR("select() timed out");
    } else if (ret < 0) {
      return CF_ERRNO("select() failed");
    }

    auto names = GetCreatedFileListFromInotifyFd(inotify);

    CF_EXPECT(names.size() > 0,
              "Failed to get names from inotify " << strerror(errno));

    if (Contains(names, filename)) {
      return {};
    }
  }

  return CF_ERR("This shouldn't be executed");
}

auto WaitForFile(const std::string& path, int timeoutSec)
    -> decltype(WaitForFileInternal(path, timeoutSec, 0)) {
  android::base::unique_fd inotify(inotify_init1(IN_CLOEXEC));

  CF_EXPECT(WaitForFileInternal(path, timeoutSec, inotify.get()));

  return {};
}

Result<void> WaitForUnixSocket(const std::string& path, int timeoutSec) {
  const auto targetTime =
      std::chrono::system_clock::now() + std::chrono::seconds(timeoutSec);

  CF_EXPECT(WaitForFile(path, timeoutSec),
            "Waiting for socket path creation failed");
  CF_EXPECT(FileIsSocket(path), "Specified path is not a socket");

  while (true) {
    const auto currentTime = std::chrono::system_clock::now();

    if (currentTime >= targetTime) {
      return CF_ERR("Timed out");
    }

    const auto timeRemain = std::chrono::duration_cast<std::chrono::seconds>(
                                targetTime - currentTime)
                                .count();
    auto testConnect =
        SharedFD::SocketLocalClient(path, false, SOCK_STREAM, timeRemain);

    if (testConnect->IsOpen()) {
      return {};
    }

    sched_yield();
  }

  return CF_ERR("This shouldn't be executed");
}
#endif

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
  if (path == "~" || android::base::StartsWith(path, "~/")) {
    const auto home_dir =
        path_info.home_dir.value_or(CF_EXPECT(SystemWideUserHome()));
    prefix = android::base::Tokenize(home_dir, "/");
  } else if (!android::base::StartsWith(path, "/")) {
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
  CF_EXPECT(android::base::StartsWith(working_dir, '/'),
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

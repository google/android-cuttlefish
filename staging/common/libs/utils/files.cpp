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

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <libgen.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
#include <ostream>
#include <ratio>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/macros.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

bool FileExists(const std::string& path, bool follow_symlinks) {
  struct stat st {};
  return (follow_symlinks ? stat : lstat)(path.c_str(), &st) == 0;
}

bool FileHasContent(const std::string& path) {
  return FileSize(path) > 0;
}

std::vector<std::string> DirectoryContents(const std::string& path) {
  std::vector<std::string> ret;
  std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(path.c_str()), closedir);
  CHECK(dir != nullptr) << "Could not read from dir \"" << path << "\"";
  if (dir) {
    struct dirent* ent{};
    while ((ent = readdir(dir.get()))) {
      ret.push_back(ent->d_name);
    }
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

Result<void> EnsureDirectoryExists(const std::string& directory_path) {
  if (DirectoryExists(directory_path)) {
    return {};
  }
  const auto parent_dir = cpp_dirname(directory_path);
  if (parent_dir.size() > 1) {
    EnsureDirectoryExists(parent_dir);
  }
  LOG(DEBUG) << "Setting up " << directory_path;
  if (mkdir(directory_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) <
          0 &&
      errno != EEXIST) {
    return CF_ERRNO("Failed to create dir: \"" << directory_path);
  }
  return {};
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
    const auto bytes_written =
        TEMP_FAILURE_RETRY(sendfile(out_fd, in_fd, offset, count));
    if (bytes_written <= 0) {
      return false;
    }

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
  std::chrono::seconds seconds(st.st_mtim.tv_sec);
  return std::chrono::system_clock::time_point(seconds);
}

bool RenameFile(const std::string& old_name, const std::string& new_name) {
  LOG(DEBUG) << "Renaming " << old_name << " to " << new_name;
  if(rename(old_name.c_str(), new_name.c_str())) {
    LOG(ERROR) << "File rename failed due to " << strerror(errno);
    return false;
  }

  return true;
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
  contents.resize(in.tellg());
  in.seekg(0, std::ios::beg);
  in.read(&contents[0], contents.size());
  in.close();
  return(contents);
}

std::string CurrentDirectory() {
  char* path = getcwd(nullptr, 0);
  if (path == nullptr) {
    PLOG(ERROR) << "`getcwd(nullptr, 0)` failed";
    return "";
  }
  std::string ret(path);
  free(path);
  return ret;
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
  char* copy = strdup(str.c_str()); // dirname may modify its argument
  std::string ret(dirname(copy));
  free(copy);
  return ret;
}

bool FileIsSocket(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}


}  // namespace cuttlefish

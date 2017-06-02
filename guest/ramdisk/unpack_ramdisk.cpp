/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "guest/ramdisk/unpack_ramdisk.h"

#include <stdio.h>
#include <stdlib.h>
#include <cstdint>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include <glog/logging.h>

#include "common/libs/auto_resources/auto_resources.h"
#include "guest/ramdisk/compressed_file_reader.h"

static const int IGNORE_CHECK_TAG = 0x070701;
static const int USE_CHECK_TAG = 0x07070;

/**
 * Description of a Ramdisk field which is a 0 filled fixed-width hexadecimal
 * number with LEN characters. The AFTER parameter allows us to calculate an
 * offset to the field by examining the length and offset of the previous field.
 */
template <typename AFTER, int LEN> struct RamdiskField {
  static uint64_t get(const char* buffer) {
    // Sadly all of the standard text to hex conversions expect a terminated
    // string, so we make a copy.
    AutoFreeBuffer temp;
    if (!temp.Resize(len_ + 1)) {
      LOG(ERROR) << "unable to allocate buffer for " << len_ << " bytes "
                 << ": " << strerror(errno);
      return 0;
    }
    memcpy(temp.data(), buffer + offset_, len_);
    errno = 0;
    const char* base = temp.data();
    char* end;
    uint64_t rval = strtoll(base, &end, 16);
    if (errno || ((end - base) != len_)) {
      LOG(ERROR) << "bad value offset=" << offset_
                 << ", value=\"" << base << "\": " << strerror(errno);
    }
    return rval;
  }

  static const int offset_ = AFTER::offset_ + AFTER::len_;
  static const int len_ = LEN;
};

/** This class allows us to define the first RamdiskField */
struct StartOfRecord {
  static const int offset_ = 0;
  static const int len_ = 0;
};

struct RamdiskRecord {
#define DEFINE_FIELD_AND_GETTER(AFTER, LEN, NAME) \
  typedef RamdiskField<AFTER, LEN> NAME; \
  uint64_t get##NAME() const { return NAME::get(buffer_); }

  DEFINE_FIELD_AND_GETTER(StartOfRecord, 6, Tag)
  DEFINE_FIELD_AND_GETTER(Tag, 8, INode)
  DEFINE_FIELD_AND_GETTER(INode, 8, Mode)
  DEFINE_FIELD_AND_GETTER(Mode, 8, Uid)
  DEFINE_FIELD_AND_GETTER(Uid, 8, Gid)
  DEFINE_FIELD_AND_GETTER(Gid, 8, NLink)
  DEFINE_FIELD_AND_GETTER(NLink, 8, MTime)
  DEFINE_FIELD_AND_GETTER(MTime, 8, DataSize)
  DEFINE_FIELD_AND_GETTER(DataSize, 8, VolMajor)
  DEFINE_FIELD_AND_GETTER(VolMajor, 8, VolMinor)
  DEFINE_FIELD_AND_GETTER(VolMinor, 8, DevMajor)
  DEFINE_FIELD_AND_GETTER(DevMajor, 8, DevMinor)
  DEFINE_FIELD_AND_GETTER(DevMinor, 8, NameLen)
  DEFINE_FIELD_AND_GETTER(NameLen, 8, CheckSum)

#undef DEFINE_FIELD_AND_GETTER

  void print() const {
    LOG(INFO) << "  tag=0x" << std::hex << getTag() << std::dec;
    LOG(INFO) << "  inode=" << getINode();
    LOG(INFO) << "  mode=0" << std::oct << getMode() << std::hex
              << " fmt=0x" << (getMode() & S_IFMT) << std::dec;
    LOG(INFO) << "  uid=" << getUid();
    LOG(INFO) << "  gid=" << getGid();
    LOG(INFO) << "  nlink=" << getNLink();
    time_t mtime = getMTime();
    struct tm local;
    char outstr[80];
    localtime_r(&mtime, &local);
    strftime(outstr, sizeof(outstr), "%a, %d %b %Y %T %z", &local);

    LOG(INFO) << "  mtime=" << getMTime() << " (" <<  outstr << ")";
    LOG(INFO) << "  data_size=" << getDataSize();
    LOG(INFO) << "  vol_major=" << getVolMajor();
    LOG(INFO) << "  vol_minor=" << getVolMinor();
    LOG(INFO) << "  dev_major=" << getDevMajor();
    LOG(INFO) << "  dev_minor=" << getDevMinor();
    LOG(INFO) << "  name_len=" << getNameLen();
    LOG(INFO) << "  checksum=" << getCheckSum();
  }

  static const int SIZE = CheckSum::offset_ + CheckSum::len_;
  char buffer_[SIZE];
};


// Helper function to set up the copy.
// TODO(ghartman): worry about the uid and gid.
static bool Copy(const AutoFreeBuffer& path, int permissions,
                 CompressedFileReader* in, uint64_t length) {
  AutoCloseFileDescriptor out_fd(
      TEMP_FAILURE_RETRY(creat(path.data(), permissions)));
  if (out_fd.IsError()) {
    LOG(ERROR) << "Skipping " << path.data() << ": open failed: "
               << strerror(errno);
    in->Skip(length);
    return true;
  }
  return length == in->Copy(length, path.data(), out_fd);
}

/**
 * Does the bulk of the work of the unpack. However, we need to change
 * the umask and want to make certain that we restore it on all code paths.
 */
static void UnpackRamdiskInner(const char* in_path, const char* out_path) {
  CompressedFileReader in(in_path);
  AutoFreeBuffer filename;

  while (true) {
    RamdiskRecord header;
    size_t num_read = in.Read(RamdiskRecord::SIZE, header.buffer_);
    if (num_read != RamdiskRecord::SIZE) {
      LOG(ERROR) << "Read failed: wanted " << RamdiskRecord::SIZE
                 << ", got " << num_read << ": " << in.ErrorString();
      return;
    }
    switch (header.getTag()) {
      case IGNORE_CHECK_TAG:  // fall through
      case USE_CHECK_TAG:
        break;
      default:
        LOG(ERROR) << "stopping due to bad header "
                   << std::hex << header.getTag() << std::dec;
        return;
    }
    if (header.getNameLen() > PATH_MAX) {
      LOG(ERROR) << "skipping file: path is too long: " << header.getNameLen();
      in.Skip(header.getNameLen());
      in.Align(4);
      in.Skip(header.getDataSize());
      in.Align(4);
      continue;
    }
    filename.Resize(header.getNameLen());
    num_read = in.Read(filename.size(), filename.data());
    if (num_read != filename.size()) {
      LOG(ERROR) << "EOF during filename read: " << in.ErrorString();
      return;
    }
    if (filename.data()[filename.size() - 1]) {
      // Add the missing NULL.
      filename.Resize(filename.size() + 1);
      LOG(ERROR) << "Stopping because " << filename.data()
                 << "doesn't end with \\0: " << in.ErrorString();
      return;
    }
    in.Align(4);
    uint64_t mode = header.getMode();
    if (!strcmp(filename.data(), "TRAILER!!!")) {
      return;
    }
    AutoFreeBuffer path;
    path.PrintF("%s/%s", out_path, filename.data());
    // TODO(ghartman): Sanitize the path coming from the file.
    int permissions = mode & 07777; // ALLPERMS isn't available.
    // Needed only for symlinks, but switch gets grumpy.
    AutoFreeBuffer target;
    int rval = 0;
    switch (mode & S_IFMT) {
      case S_IFREG:
        if (!Copy(path, permissions, &in, header.getDataSize())) {
          return;
        }
        break;
      case S_IFLNK:
        target.Resize(header.getDataSize() + 1);
        in.Read(header.getDataSize(), target.data());
        // We know this will be null terminated because auto-free buffers
        // initialize to 0.
        rval = TEMP_FAILURE_RETRY(symlink(target.data(), path.data()));
        if (rval == -1) {
          LOG(ERROR) << "skipping " << path.data() << ": symlink to "
                     << target.data() << "  failed: " << strerror(errno);
        }
        break;
      case S_IFDIR:
        // TODO(ghartman): Fix ownership of the directory.
        rval = TEMP_FAILURE_RETRY(mkdir(path.data(), permissions));
        if ((rval == -1) && (errno != EEXIST)) {
          LOG(ERROR) << "skipping " << path.data() << ": mkdir failed: "
               << strerror(errno);
        }
        break;
      default:
        LOG(ERROR) << "skipping " << path.data() << ": unknown mode "
                   << std::oct << (mode & S_IFMT) << std::dec;
        in.Skip(header.getDataSize());
    }
    in.Align(4);
  }
}

/**
 * Unpack a compressed ramdisk at in_path, appending out_path to its
 * embedded pathnames.
 */
void UnpackRamdisk(const char* in_path, const char* out_path) {
  mode_t saved = umask(0);
  UnpackRamdiskInner(in_path, out_path);
  umask(saved);
}

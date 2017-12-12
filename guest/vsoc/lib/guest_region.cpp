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
#include "guest/vsoc/lib/guest_region.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <thread>

#include <android-base/logging.h>
#include <uapi/vsoc_shm.h>

using avd::SharedFD;

namespace {

std::string device_path_from_name(const char* region_name) {
  return std::string("/dev/") + region_name;
}

}

bool vsoc::OpenableRegion::Open(const char* region_name) {
  std::string path = device_path_from_name(region_name);
  region_fd_ = SharedFD::Open(path.c_str(), O_RDWR);
  if (!region_fd_->IsOpen()) {
    LOG(FATAL) << "Unable to open region " << region_name << " ("
               << region_fd_->StrError() << ")";
    return false;
  }
  if (region_fd_->Ioctl(VSOC_DESCRIBE_REGION, &region_desc_)) {
    LOG(FATAL) << "Unable to obtain region descriptor ("
               << region_fd_->StrError() << ")";
    return false;
  }
  region_base_ =
      region_fd_->Mmap(0, region_size(), PROT_READ | PROT_WRITE, MAP_SHARED, 0);
  if (region_base_ == MAP_FAILED) {
    LOG(FATAL) << "mmap failed (" << region_fd_->StrError() << ")";
    return false;
  }
  return true;
}

void vsoc::OpenableRegion::InterruptPeer() {
  // TOOD: move the atomic exchange from the kernel to save the system
  // call in cases were we don't want to post an interrupt.
  // This has the added advantage of lining the code up with
  // HostRegion::InterruptPeer()
  if ((region_fd_->Ioctl(VSOC_MAYBE_SEND_INTERRUPT_TO_HOST, 0) == -1) &&
      (errno != EBUSY)) {
    LOG(INFO) << __FUNCTION__ << ": ioctl failed (" << strerror(errno) << ")";
  }
}

void vsoc::OpenableRegion::InterruptSelf() {
  region_fd_->Ioctl(VSOC_SELF_INTERRUPT, 0);
}

void vsoc::OpenableRegion::WaitForInterrupt() {
  region_fd_->Ioctl(VSOC_WAIT_FOR_INCOMING_INTERRUPT, 0);
}

int vsoc::OpenableRegion::CreateFdScopedPermission(
    const char* managed_region_name,
    uint32_t* owner_ptr,
    uint32_t owned_value,
    vsoc_reg_off_t begin_offset,
    vsoc_reg_off_t end_offset) {
  if (!region_fd_->IsOpen()) {
    LOG(FATAL) << "Can't create permission before opening controller region";
    return -EINVAL;
  }
  int managed_region_fd =
      open(device_path_from_name(managed_region_name).c_str(), O_RDWR);
  if (managed_region_fd < 0) {
    int errno_ = errno;
    LOG(FATAL) << "Can't open managed region: " << managed_region_name;
    return -errno_;
  }

  fd_scoped_permission_arg perm;
  perm.perm.begin_offset = begin_offset;
  perm.perm.end_offset = end_offset;
  perm.perm.owned_value = owned_value;
  perm.perm.owner_offset = pointer_to_region_offset(owner_ptr);
  perm.managed_region_fd = managed_region_fd;
  LOG(INFO) << "owner offset: " << perm.perm.owner_offset;
  int retval = region_fd_->Ioctl(VSOC_CREATE_FD_SCOPED_PERMISSION, &perm);
  if (retval) {
    int errno_ = errno;
    close(managed_region_fd);
    if (errno != EBUSY) {
        LOG(FATAL) << "Unable to create fd scoped permission (" <<
          strerror(errno) << ")";
    }
    return -errno_;
  }
  return managed_region_fd;
}

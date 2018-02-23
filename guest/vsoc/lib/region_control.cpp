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
#include "common/vsoc/lib/region_view.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <thread>

#include <android-base/logging.h>
#include <uapi/vsoc_shm.h>

using cvd::SharedFD;

namespace {
class GuestRegionControl : public vsoc::RegionControl {
 public:
  explicit GuestRegionControl(const SharedFD& region_fd,
                              const vsoc_device_region& desc)
      : region_fd_{region_fd} {
    region_desc_ = desc;
  }
  virtual bool InterruptPeer() override;
  virtual void InterruptSelf() override;
  virtual void WaitForInterrupt() override;
  virtual void* Map() override;

 protected:
  int CreateFdScopedPermission(const char* managed_region_name,
                               uint32_t owner_offset, uint32_t owned_val,
                               uint32_t begin_offset,
                               uint32_t end_offset) override;
  cvd::SharedFD region_fd_;
};

std::string device_path_from_name(const char* region_name) {
  return std::string("/dev/") + region_name;
}

bool GuestRegionControl::InterruptPeer() {
  int rval = region_fd_->Ioctl(VSOC_SEND_INTERRUPT_TO_HOST, 0);
  if ((rval == -1) && (errno != EBUSY)) {
    LOG(INFO) << __FUNCTION__ << ": ioctl failed (" << strerror(errno) << ")";
  }
  return !rval;
}

void GuestRegionControl::InterruptSelf() {
  region_fd_->Ioctl(VSOC_SELF_INTERRUPT, 0);
}

void GuestRegionControl::WaitForInterrupt() {
  region_fd_->Ioctl(VSOC_WAIT_FOR_INCOMING_INTERRUPT, 0);
}

int GuestRegionControl::CreateFdScopedPermission(
    const char* managed_region_name, uint32_t owner_offset,
    uint32_t owned_value, uint32_t begin_offset,
    uint32_t end_offset) {
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
  perm.perm.owner_offset = owner_offset;
  perm.managed_region_fd = managed_region_fd;
  LOG(INFO) << "owner offset: " << perm.perm.owner_offset;
  int retval = region_fd_->Ioctl(VSOC_CREATE_FD_SCOPED_PERMISSION, &perm);
  if (retval) {
    int errno_ = errno;
    close(managed_region_fd);
    if (errno_ != EBUSY) {
      LOG(FATAL) << "Unable to create fd scoped permission ("
                 << strerror(errno_) << ")";
    }
    return -errno_;
  }
  return managed_region_fd;
}

void* GuestRegionControl::Map() {
  region_base_ =
      region_fd_->Mmap(0, region_size(), PROT_READ | PROT_WRITE, MAP_SHARED, 0);
  if (region_base_ == MAP_FAILED) {
    LOG(FATAL) << "mmap failed (" << region_fd_->StrError() << ")";
    region_base_ = nullptr;
  }
  return region_base_;
}
}  // namespace

// domain is here to ensure that this method has the same signature as the
// method on host regions.
std::shared_ptr<vsoc::RegionControl> vsoc::RegionControl::Open(
    const char* region_name) {
  std::string path = device_path_from_name(region_name);
  SharedFD fd = SharedFD::Open(path.c_str(), O_RDWR);
  if (!fd->IsOpen()) {
    LOG(FATAL) << "Unable to open region " << region_name << " ("
               << fd->StrError() << ")";
    return nullptr;
  }
  vsoc_device_region desc;
  if (fd->Ioctl(VSOC_DESCRIBE_REGION, &desc)) {
    LOG(FATAL) << "Unable to obtain region descriptor (" << fd->StrError()
               << ")";
    return nullptr;
  }
  return std::shared_ptr<vsoc::RegionControl>(new GuestRegionControl(fd, desc));
}

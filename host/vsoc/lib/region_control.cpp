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
#include "host/vsoc/lib/region_control.h"
#include "common/vsoc/lib/region_view.h"

#define LOG_TAG "vsoc: region_host"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

using avd::SharedFD;

const char vsoc_user_prefix[] = "vsoc-";

int GetDefaultInstance() {
  char* user = getenv("USER");
  if (user && !memcmp(user, vsoc_user_prefix, sizeof(vsoc_user_prefix) - 1)) {
    int temp = atoi(user + sizeof(vsoc_user_prefix) - 1);
    if (temp > 0) {
      return temp;
    }
  }
  return 1;
}

DEFINE_int32(instance, GetDefaultInstance(),
             "Instance number. Must be unique.");

namespace {

class HostRegionControl : public vsoc::RegionControl {
 public:
  HostRegionControl(const char* region_name,
                    const SharedFD& incoming_interrupt_fd,
                    const SharedFD& outgoing_interrupt_fd,
                    const SharedFD& shared_memory_fd)
      : region_name_{region_name},
        incoming_interrupt_fd_{incoming_interrupt_fd},
        outgoing_interrupt_fd_{outgoing_interrupt_fd},
        shared_memory_fd_{shared_memory_fd} {}

  int CreateFdScopedPermission(const char* managed_region_name,
                               vsoc_reg_off_t owner_offset, uint32_t owned_val,
                               vsoc_reg_off_t begin_offset,
                               vsoc_reg_off_t end_offset) override {
    return -1;
  }

  bool InitializeRegion();

  virtual bool InterruptPeer() override {
    uint64_t one = 1;
    ssize_t rval = outgoing_interrupt_fd_->Write(&one, sizeof(one));
    if (rval != sizeof(one)) {
      LOG(FATAL) << __FUNCTION__ << ": rval (" << rval << ") != sizeof(one))";
      return false;
    }
    return true;
  }

  // Wake the local signal table scanner. Primarily used during shutdown
  virtual void InterruptSelf() override {
    uint64_t one = 1;
    ssize_t rval = incoming_interrupt_fd_->Write(&one, sizeof(one));
    if (rval != sizeof(one)) {
      LOG(FATAL) << __FUNCTION__ << ": rval (" << rval << ") != sizeof(one))";
    }
  }

  virtual void WaitForInterrupt() override {
    // Check then act isn't a problem here: the other side does
    // the following things in exactly this order:
    //   1. exchanges 1 with interrupt_signalled
    //   2. if interrupt_signalled was 0 it increments the eventfd
    // eventfd increments are persistent, so if interrupt_signalled was set
    // back to 1 while we are going to sleep the sleep will return
    // immediately.
    uint64_t missed{};
    avd::SharedFDSet readset;
    readset.Set(incoming_interrupt_fd_);
    avd::Select(&readset, NULL, NULL, NULL);
    ssize_t rval = incoming_interrupt_fd_->Read(&missed, sizeof(missed));
    if (rval != sizeof(missed)) {
      LOG(FATAL) << __FUNCTION__ << ": rval (" << rval
                 << ") != sizeof(missed))";
    }
    if (!missed) {
      LOG(FATAL) << __FUNCTION__ << ": woke with 0 interrupts";
    }
  }

  virtual void* Map() override {
    if (region_base_) {
      return region_base_;
    }
    // Now actually map the region
    region_base_ =
        shared_memory_fd_->Mmap(0, region_size(), PROT_READ | PROT_WRITE,
                                MAP_SHARED, region_desc_.region_begin_offset);
    if (region_base_ == MAP_FAILED) {
      LOG(FATAL) << "mmap failed for offset "
                 << region_desc_.region_begin_offset << " ("
                 << shared_memory_fd_->StrError() << ")";
      region_base_ = nullptr;
    }
    return region_base_;
  }

 protected:
  const char* region_name_{};
  avd::SharedFD incoming_interrupt_fd_;
  avd::SharedFD outgoing_interrupt_fd_;
  avd::SharedFD shared_memory_fd_;
};

// Default path to the ivshmem_server socket. This can vary when we're
// launching multiple AVDs.
constexpr int kMaxSupportedProtocolVersion = 0;

bool HostRegionControl::InitializeRegion() {
  size_t region_name_len = strlen(region_name_);
  if (region_name_len >= sizeof(vsoc_device_name)) {
    LOG(FATAL) << "Region name length (" << region_name_len << ") not < "
               << sizeof(vsoc_device_name);
    return false;
  }
  vsoc_shm_layout_descriptor layout;
  ssize_t rval = shared_memory_fd_->Pread(&layout, sizeof(layout), 0);
  if (rval != sizeof(layout)) {
    LOG(FATAL) << "Unable to read layout, rval=" << rval << " ("
               << shared_memory_fd_->StrError() << ")";
    return false;
  }
  if (layout.major_version != CURRENT_VSOC_LAYOUT_MAJOR_VERSION) {
    LOG(FATAL) << "Incompatible major version: saw " << layout.major_version
               << " wanted " << CURRENT_VSOC_LAYOUT_MAJOR_VERSION;
  }
  std::vector<vsoc_device_region> descriptors;
  descriptors.resize(layout.region_count);
  ssize_t wanted = sizeof(vsoc_device_region) * layout.region_count;
  rval = shared_memory_fd_->Pread(descriptors.data(), wanted,
                                  layout.vsoc_region_desc_offset);
  if (rval != wanted) {
    LOG(FATAL) << "Unable to read region descriptors, rval=" << rval << " ("
               << shared_memory_fd_->StrError() << ")";
    return false;
  }
  for (const auto& desc : descriptors) {
    if (!strcmp(region_name_, desc.device_name)) {
      region_desc_ = desc;
      return true;
    }
  }

  std::ostringstream buf;
  for (const auto& desc : descriptors) {
    buf << " " << desc.device_name;
  }
  LOG(FATAL) << "Region name of " << region_name_
             << " not found among:" << buf.str();
  return false;
}
}  // namespace

std::string vsoc::GetPerInstanceDefault(const char* prefix) {
  std::ostringstream stream;
  stream << prefix << std::setfill('0') << std::setw(2)
         << GetDefaultInstance();
  return stream.str();
}

std::string vsoc::GetPerInstanceDir() {
  return vsoc::GetPerInstanceDefault("/var/run/cvd-");
}

std::string vsoc::GetPerInstancePath(const std::string& basename) {
  std::ostringstream stream;
  stream << GetPerInstanceDir() << "/" << basename;
  return stream.str();
}

std::string vsoc::GetShmClientSocketPath() {
  return vsoc::GetPerInstancePath("ivshmem_socket_client");
}

std::shared_ptr<vsoc::RegionControl> vsoc::RegionControl::Open(
    const char* region_name, const char* domain) {
  AutoFreeBuffer msg;
  std::string owned_domain;

  if (!domain) {
    owned_domain = GetShmClientSocketPath();
    domain = owned_domain.c_str();
  }
  SharedFD region_server =
      SharedFD::SocketLocalClient(domain, false, SOCK_STREAM);
  if (!region_server->IsOpen()) {
    LOG(FATAL) << "Could not contact ivshmem_server ("
               << region_server->StrError() << ")";
    return nullptr;
  }

  // Check server protocol version.
  uint32_t protocol_version;
  ssize_t bytes = region_server->Recv(&protocol_version,
                                      sizeof(protocol_version), MSG_NOSIGNAL);
  if (bytes != sizeof(protocol_version)) {
    LOG(FATAL) << "Failed to recv protocol version; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return nullptr;
  }

  if (protocol_version > kMaxSupportedProtocolVersion) {
    LOG(FATAL) << "Unsupported protocol version " << protocol_version
               << "; max supported version is " << kMaxSupportedProtocolVersion;
    return nullptr;
  }

  // Send requested region.
  int16_t size = strlen(region_name);
  bytes = region_server->Send(&size, sizeof(size), MSG_NOSIGNAL);
  if (bytes != sizeof(size)) {
    LOG(FATAL) << "Failed to send region name length; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return nullptr;
  }

  bytes = region_server->Send(region_name, size, MSG_NOSIGNAL);
  if (bytes != size) {
    LOG(FATAL) << "Failed to send region name; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return nullptr;
  }

  // Receive control sockets.
  uint64_t control_data;
  struct iovec iov {
    &control_data, sizeof(control_data)
  };
  avd::InbandMessageHeader hdr{};
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  SharedFD fds[3];
  bytes = region_server->RecvMsgAndFDs(hdr, 0, &fds);
  if (bytes != sizeof(control_data)) {
    LOG(FATAL) << "Failed to complete handshake; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return nullptr;
  }
  HostRegionControl* rval =
      new HostRegionControl(region_name, fds[0], fds[1], fds[2]);
  if (!rval) {
    return nullptr;
  }
  // Search for the region header
  if (!rval->InitializeRegion()) {
    // We already logged, so we can just bail out.
    return nullptr;
  }
  return std::shared_ptr<RegionControl>(rval);
}

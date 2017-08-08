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
#include "host/vsoc/lib/host_region.h"

#define LOG_TAG "vsoc: region_host"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <sstream>
#include <thread>
#include <vector>

#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"

using avd::SharedFD;

namespace {
// Default path to the ivshmem_server socket. This can vary when we're
// launching multiple AVDs.
const char DEFAULT_DOMAIN[] = "/tmp/ivshmem_socket_client";

constexpr int kMaxSupportedProtocolVersion = 0;

bool InitializeRegion(const SharedFD& fd, const char* region_name,
                      vsoc_device_region* dest) {
  size_t region_name_len = strlen(region_name);
  if (region_name_len >= sizeof(vsoc_device_name)) {
    LOG(FATAL) << "Region name length (" << region_name_len << ") not < "
               << sizeof(vsoc_device_name);
    return false;
  }
  vsoc_shm_layout_descriptor layout;
  ssize_t rval = fd->Pread(&layout, sizeof(layout), 0);
  if (rval != sizeof(layout)) {
    LOG(FATAL) << "Unable to read layout, rval=" << rval << " ("
               << fd->StrError() << ")";
    return false;
  }
  if (layout.major_version != CURRENT_VSOC_LAYOUT_MAJOR_VERSION) {
    LOG(FATAL) << "Incompatible major version: saw " << layout.major_version
               << " wanted " << CURRENT_VSOC_LAYOUT_MAJOR_VERSION;
  }
  std::vector<vsoc_device_region> descriptors;
  descriptors.resize(layout.region_count);
  ssize_t wanted = sizeof(vsoc_device_region) * layout.region_count;
  rval = fd->Pread(descriptors.data(), wanted, layout.vsoc_region_desc_offset);
  if (rval != wanted) {
    LOG(FATAL) << "Unable to read region descriptors, rval=" << rval << " ("
               << fd->StrError() << ")";
    return false;
  }
  for (const auto& desc : descriptors) {
    if (!strcmp(region_name, desc.device_name)) {
      *dest = desc;
      return true;
    }
  }

  std::ostringstream buf;
  for (const auto& desc : descriptors) {
    buf << " " << desc.device_name;
  }
  LOG(FATAL) << "Region name of " << region_name
             << " not found among:" << buf.str();
  return false;
}
}  // namespace

bool vsoc::OpenableRegionView::Open(const char* region_name, const char* domain) {
  AutoFreeBuffer msg;

  if (!domain) {
    domain = DEFAULT_DOMAIN;
  }
  SharedFD region_server =
      SharedFD::SocketLocalClient(domain, false, SOCK_STREAM);
  if (!region_server->IsOpen()) {
    LOG(FATAL) << "Could not contact ivshmem_server ("
               << region_server->StrError() << ")";
    return false;
  }

  // Check server protocol version.
  uint32_t protocol_version;
  ssize_t bytes = region_server->Recv(&protocol_version,
                                      sizeof(protocol_version), MSG_NOSIGNAL);
  if (bytes != sizeof(protocol_version)) {
    LOG(FATAL) << "Failed to recv protocol version; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return false;
  }

  if (protocol_version > kMaxSupportedProtocolVersion) {
    LOG(FATAL) << "Unsupported protocol version " << protocol_version
               << "; max supported version is " << kMaxSupportedProtocolVersion;
    return false;
  }

  // Send requested region.
  int16_t size = strlen(region_name);
  bytes = region_server->Send(&size, sizeof(size), MSG_NOSIGNAL);
  if (bytes != sizeof(size)) {
    LOG(FATAL) << "Failed to send region name length; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return false;
  }

  bytes = region_server->Send(region_name, size, MSG_NOSIGNAL);
  if (bytes != size) {
    LOG(FATAL) << "Failed to send region name; res=" << bytes << " ("
               << region_server->StrError() << ")";
    return false;
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
    return false;
  }
  incoming_interrupt_fd_ = fds[0];
  outgoing_interrupt_fd_ = fds[1];
  SharedFD shared_memory_fd = fds[2];

  // Search for the region header
  if (!InitializeRegion(shared_memory_fd, region_name, &region_desc_)) {
    // We already logged, so we can just bail out.
    return false;
  }
  // Now actually map the region
  region_base_ =
      shared_memory_fd->Mmap(0, region_size(), PROT_READ | PROT_WRITE,
                             MAP_SHARED, region_desc_.region_begin_offset);
  if (region_base_ == MAP_FAILED) {
    LOG(FATAL) << "mmap failed for offset " << region_desc_.region_begin_offset
               << " (" << shared_memory_fd->StrError() << ")";
    return false;
  }
  return true;
}

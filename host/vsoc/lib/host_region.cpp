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
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>

#include <thread>
#include <vector>

#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"

using avd::SharedFD;

namespace {
// Default path to the ivshmem_server socket. This can vary when we're
// launching multiple AVDs.
const char DEFAULT_DOMAIN[] = "/tmp/ivshmem_socket_client";

// These contants define the protocol in ivshmem_server.py
// See ivshmem-server in the host-side repository for more information.
// TODO(ghartman): Replace the the official protocol when it's ready.

// Not really defined, but most of the responses seem to fit on a single line.
const size_t PROTOCOL_MESSAGE_SIZE = 80;
const char GET_PROTOCOL_VER_MSG[] = "GET PROTOCOL_VER";
const char EXPECTED_PROTOCOL_VER_RESP[] = "0x00000000";
const char FMT_REGION_NAME_MSG[] = "INFORM REGION_NAME_LEN: 0x%08x";
const char REGION_NOT_FOUND_RESP[] = "0xffffffff";

bool InitializeRegion(const SharedFD& fd, const char* region_name,
                      vsoc_device_region* dest) {
  size_t region_name_len = strlen(region_name);
  if (region_name_len >= sizeof(vsoc_device_name)) {
    LOG(FATAL) << "Region name length (" << region_name_len << ") not < " <<
        sizeof(vsoc_device_name);
    return false;
  }
  vsoc_shm_layout_descriptor layout;
  ssize_t rval = fd->Pread(&layout, sizeof(layout), 0);
  if (rval != sizeof(layout)) {
    LOG(FATAL) <<  "Unable to read layout, rval=" << rval << " (" <<
        fd->StrError() << ")";
    return false;
  }
  if (layout.major_version != CURRENT_VSOC_LAYOUT_MAJOR_VERSION) {
    LOG(FATAL) << "Incompatible major version: saw " << layout.major_version <<
        " wanted " << CURRENT_VSOC_LAYOUT_MAJOR_VERSION;
  }
  std::vector<vsoc_device_region> descriptors;
  descriptors.resize(layout.region_count);
  ssize_t wanted = sizeof(vsoc_device_region) * layout.region_count;
  rval = fd->Pread(descriptors.data(), wanted, layout.vsoc_region_desc_offset);
  if (rval != wanted) {
    LOG(FATAL) << "Unable to read region descriptors, rval=" << rval << " (" <<
        fd->StrError() << ")";
    return false;
  }
  for (const auto& desc : descriptors) {
    if (!strcmp(region_name, desc.device_name)) {
      *dest = desc;
      return true;
    }
  }
  LOG(FATAL) << "Region name of " << region_name << " not found among:";
  for (const auto& desc : descriptors) {
    LOG(FATAL) << "  " << desc.device_name;
  }
  return false;
}

}

bool vsoc::OpenableRegion::Open(const char* region_name, const char* domain) {
  AutoFreeBuffer msg;

  if (!domain) {
    domain = DEFAULT_DOMAIN;
  }
  SharedFD region_server = SharedFD::SocketLocalClient(
      domain, false, SOCK_STREAM);
  if (!region_server->IsOpen()) {
    LOG(FATAL) << "Could not contact ivshmem_server (" <<
        region_server->StrError() << ")";
    return false;
  }
  AutoFreeBuffer buffer;
  buffer.Resize(PROTOCOL_MESSAGE_SIZE);
  ssize_t bytes = region_server->Send(
      GET_PROTOCOL_VER_MSG, sizeof(GET_PROTOCOL_VER_MSG) - 1, 0);
  if (bytes != sizeof(GET_PROTOCOL_VER_MSG) - 1) {
    LOG(FATAL) << "Short send on GET_PROTOCOL_VER_MSG rval=" << bytes << " ("
               << region_server->StrError() << ")";
    return false;
  }
  bytes = region_server->Recv(buffer.data(), buffer.size(), 0);
  if ((bytes != sizeof(EXPECTED_PROTOCOL_VER_RESP) - 1) ||
      memcmp(buffer.data(), EXPECTED_PROTOCOL_VER_RESP, bytes)) {
    // Null terminate the response
    buffer.Resize(bytes);
    buffer.Resize(bytes+1);
    LOG(FATAL) << "Unexpected PROTOCOL_VER " << bytes << " bytes, value " <<
        buffer.data() << " (" << region_server->StrError() << ")";
    return false;
  }
  msg.PrintF(FMT_REGION_NAME_MSG, strlen(region_name));
  // Don't transmit the \0
  bytes = region_server->Send(msg.data(), msg.size() - 1, 0);
  if (bytes != msg.size() - 1) {
    LOG(FATAL) << "Short send on REGION_NAME_LEN rval=" << bytes << " (" <<
        region_server->StrError() << ")";
    return false;
  }
  bytes = region_server->Send(region_name, strlen(region_name), 0);
  if (bytes != strlen(region_name)) {
    LOG(FATAL) << "Short send on REGION_NAME bytes rval=" << bytes << " (" <<
        region_server->StrError() << ")";
    return false;
  }
  bytes = region_server->Recv(buffer.data(), buffer.size(), 0);
  if ((bytes == sizeof(REGION_NOT_FOUND_RESP) - 1) &&
      !memcmp(buffer.data(), REGION_NOT_FOUND_RESP,
              sizeof(REGION_NOT_FOUND_RESP) - 1)) {
    LOG(FATAL) << "Region not found " << bytes << " (" <<
        region_server->StrError() << ")";
    return false;
  }
  bytes = region_server->Recv(buffer.data(), buffer.size(), 0);
  if (bytes == -1) {
    LOG(FATAL) << "Read error on region end offset (" <<
        region_server->StrError() << ")";
    return false;
  }
  struct iovec iov { buffer.data(), buffer.size() };
  avd::InbandMessageHeader hdr{};
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  SharedFD fds[1];
  region_server->RecvMsgAndFDs(hdr, 0, &fds);
  incoming_interrupt_fd_ = fds[0];
  region_server->RecvMsgAndFDs(hdr, 0, &fds);
  outgoing_interrupt_fd_ = fds[0];
  region_server->RecvMsgAndFDs(hdr, 0, &fds);
  // This holds all of shared memory, not just this region.
  SharedFD shared_memory_fd = fds[0];
  // Search for the region header
  if (!InitializeRegion(shared_memory_fd, region_name, &region_desc_)) {
    // We already logged, so we can just bail out.
    return false;
  }
  // Now actually map the region
  region_base_ = shared_memory_fd->Mmap(
      0, region_size(), PROT_READ|PROT_WRITE, MAP_SHARED,
      region_desc_.region_begin_offset);
  if (region_base_ == MAP_FAILED) {
    LOG(FATAL) << "mmap failed for offset " <<
        region_desc_.region_begin_offset << " (" <<
        shared_memory_fd->StrError() << ")";
    return false;
  }
  return true;
}

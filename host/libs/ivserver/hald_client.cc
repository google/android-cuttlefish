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
#include "host/libs/ivserver/hald_client.h"

#include <string>

#include <glog/logging.h>

namespace ivserver {
namespace {
// The protocol between host-clients and the ivserver could change.
// Clients should verify what version they are talking to during the handshake.
const uint32_t kHaldClientProtocolVersion = 0;
}  // anonymous namespace

std::unique_ptr<HaldClient> HaldClient::New(const VSoCSharedMemory& shmem,
                                            const cvd::SharedFD& clientfd) {
  std::unique_ptr<HaldClient> res;

  if (!clientfd->IsOpen()) {
    LOG(WARNING) << "Invalid socket passed to HaldClient: "
                 << clientfd->StrError();
    return res;
  }

  res.reset(new HaldClient(clientfd));
  if (!res->PerformHandshake(shmem)) {
    LOG(ERROR) << "HalD handshake failed. Dropping connection.";
    res.reset();
  }

  return res;
}

HaldClient::HaldClient(const cvd::SharedFD& client_socket)
    : client_socket_(client_socket) {}

bool HaldClient::PerformHandshake(const VSoCSharedMemory& shared_mem) {
  int rval =
      client_socket_->Send(&kHaldClientProtocolVersion,
                           sizeof(kHaldClientProtocolVersion), MSG_NOSIGNAL);
  if (rval != sizeof(kHaldClientProtocolVersion)) {
    LOG(ERROR) << "failed to send protocol version: "
               << client_socket_->StrError();
    return false;
  }

  int16_t region_name_len;
  if (client_socket_->Recv(&region_name_len, sizeof(region_name_len),
                           MSG_NOSIGNAL) != sizeof(region_name_len)) {
    LOG(ERROR) << "Error receiving region name length: "
               << client_socket_->StrError();
    return false;
  }

  if (region_name_len <= 0 ||
      region_name_len > VSoCSharedMemory::kMaxRegionNameLength) {
    LOG(ERROR) << "Invalid region length received: " << region_name_len;
    return false;
  }

  std::vector<char> region_name_data(region_name_len);
  rval = client_socket_->Recv(region_name_data.data(), region_name_len,
                              MSG_NOSIGNAL);
  if (rval != region_name_len) {
    LOG(ERROR) << "Incomplete region name length received. Want: "
               << region_name_len << ", got: " << rval;
    return false;
  }

  std::string region_name(region_name_data.begin(), region_name_data.end());
  LOG(INFO) << "New HALD requesting region: " << region_name;

  // Send Host, Guest and SharedMemory FDs associated with this region.
  cvd::SharedFD guest_to_host_efd;
  cvd::SharedFD host_to_guest_efd;

  if (!shared_mem.GetEventFdPairForRegion(region_name, &guest_to_host_efd,
                                          &host_to_guest_efd)) {
    LOG(ERROR) << "Region " << region_name << " was not found.";
    return false;
  }

  if (!guest_to_host_efd->IsOpen()) {
    LOG(ERROR) << "Host channel is not open; last known error: "
               << guest_to_host_efd->StrError();
    return false;
  }

  if (!host_to_guest_efd->IsOpen()) {
    LOG(ERROR) << "Guest channel is not open; last known error: "
               << host_to_guest_efd->StrError();
    return false;
  }

  // TODO(ender): delete this once no longer necessary. Currently, absence of
  // payload makes RecvMsgAndFDs hang forever.
  uint64_t control_data = 0;
  struct iovec vec {
    &control_data, sizeof(control_data)
  };
  cvd::InbandMessageHeader hdr{nullptr, 0, &vec, 1, 0};
  cvd::SharedFD fds[3] = {guest_to_host_efd, host_to_guest_efd,
                          shared_mem.SharedMemFD()};
  rval = client_socket_->SendMsgAndFDs<3>(hdr, MSG_NOSIGNAL, fds);
  if (rval == -1) {
    LOG(ERROR) << "failed to send Host FD: " << client_socket_->StrError();
    return false;
  }

  LOG(INFO) << "HALD managing region: " << region_name << " connected.";
  return true;
}

}  // namespace ivserver

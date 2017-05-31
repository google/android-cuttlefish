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
#include "guest/gce_network/dhcp_server.h"

#include <arpa/inet.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <string.h>
#include <sys/socket.h>

#include <cerrno>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "guest/gce_network/dhcp_message.h"
#include "guest/gce_network/logging.h"

namespace avd {
namespace {
const uint32_t kDhcpBroadcastAddress = ~0u;
const int32_t kDhcpServerPort = 67;
const int32_t kDhcpClientPort = 68;
const int32_t kDhcpMessageMaxLength = 4096;
// These values are defined by standard
// https://tools.ietf.org/html/rfc1191
const int32_t kMtuMinValue = 68;
const int32_t kMtuMaxValue = 1536;

class DhcpServerImpl : public DhcpServer {
 public:
  DhcpServerImpl();
  virtual ~DhcpServerImpl() {}

  virtual bool Start(const Options& options);

 private:
  // Process and validate options.
  // Returns true, if DHCP parameters appear valid.
  bool ProcessOptions(const Options& options);

  // Open port 67 for both direct requests and broadcast messages.
  // Returns false, if socket could not be opened / configured.
  bool CreateSocket();

  // Convert string representation of an IP address.
  // Returns true upon successful conversion. Converted value will be stored in
  // |result|.
  bool StringToAddr(const std::string& address, in_addr_t* result);

  // Build DHCP Response message based on the DHCP request message.
  DhcpMessage* BuildResponse(DhcpMessage* request);

  // Receive DHCP message.
  // Returns false, when no more messages can be read from the socket
  // (terminating).
  bool ReceiveDHCPMessage(std::vector<uint8_t>* data);

  // Get or allocate IP address for specified MAC address.
  // Returns 0 if address pool is exhausted and there's no already assigned IP
  // address for the specified MAC address.
  in_addr_t HWAdrdessToIPAddress(const std::vector<uint8_t>& address);

  // Send DHCP broadcast message.
  void SendDHCPMessage(const std::vector<uint8_t>& data);

  std::string bind_device_;
  in_addr_t server_ip_address_;
  in_addr_t gateway_ip_address_;
  in_addr_t start_ip_address_;
  in_addr_t end_ip_address_;
  in_addr_t network_mask_;
  in_addr_t dns_address_;
  int32_t mtu_;
  int32_t lease_time_seconds_;

  SharedFD socket_;

  // MAC address to in_addr_t map.
  std::map<std::string, in_addr_t> address_map_;
};

DhcpServerImpl::DhcpServerImpl()
    : server_ip_address_(0),
      gateway_ip_address_(0),
      start_ip_address_(0),
      end_ip_address_(0),
      network_mask_(0),
      dns_address_(0),
      mtu_(0),
      lease_time_seconds_(0) {}

bool DhcpServerImpl::StringToAddr(
    const std::string& address, in_addr_t* result) {
  // Convert "a.b.c.d" to uint32_t
  *result = inet_addr(address.c_str());
  if (*result == ~0u) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to convert %s to IP address.\n",
               __FUNCTION__, address.c_str());
    return false;
  }
  // the result is stored in network byte order, which is not what we want to
  // use. Original byte order would break logic in many places (most notably
  // validation, but also message construction).
  *result = ntohl(*result);
  return true;
}

bool DhcpServerImpl::ProcessOptions(const DhcpServer::Options& options) {
  bind_device_ = options.bind_device;
  StringToAddr(options.server_address, &server_ip_address_);
  StringToAddr(options.gateway_address, &gateway_ip_address_);
  StringToAddr(options.start_ip_address, &start_ip_address_);
  StringToAddr(options.end_ip_address, &end_ip_address_);
  StringToAddr(options.network_mask, &network_mask_);
  StringToAddr(options.dns_address, &dns_address_);

  mtu_ = options.mtu;
  lease_time_seconds_ = options.lease_time;

  // Validate MTU.
  // No MTU -> option will not be sent to client.
  if (mtu_ != 0 && (mtu_ < kMtuMinValue || mtu_ > kMtuMaxValue)) {
    KLOG_ERROR(LOG_TAG, "%s: MTU size %d not acceptable.\n",
               __FUNCTION__, mtu_);
    return false;
  }

  // Check interface has been specified.
  if (bind_device_.empty()) {
    KLOG_ERROR(LOG_TAG, "%s: No bind device specified.\n", __FUNCTION__);
    return false;
  }

  // Verify server IP address.
  if (server_ip_address_ == 0 || server_ip_address_ == ~0u) {
    KLOG_ERROR(LOG_TAG, "%s: No server IP address specified.\n", __FUNCTION__);
    return false;
  }

  // Check network mask has been specified.
  if (!network_mask_) {
    KLOG_ERROR(LOG_TAG, "%s: No network mask specified.\n", __FUNCTION__);
    return false;
  }

  // Check start IP address has been specified.
  if (!start_ip_address_) {
    KLOG_ERROR(LOG_TAG, "%s: No IP address range specified.\n", __FUNCTION__);
    return false;
  }

  // Check that start and end ip address are within same network.
  if ((start_ip_address_ & network_mask_) !=
      (end_ip_address_ & network_mask_)) {
    KLOG_ERROR(
        LOG_TAG, "%s: Start and End IP addresses do not belong to the same "
        "network (%x and %x, netmask %x)\n",
        __FUNCTION__,
        start_ip_address_, end_ip_address_, network_mask_);
    return false;
  }

  // Check that start IP address is lower than end IP address.
  if (start_ip_address_ > end_ip_address_) {
    KLOG_ERROR(LOG_TAG, "%s: Start IP address (%x) greater than end IP "
               "address (%x)\n",
               __FUNCTION__, start_ip_address_, end_ip_address_);
    return false;
  }

  // Check the lease time.
  if (lease_time_seconds_ == 0) {
    KLOG_ERROR(LOG_TAG, "%s: No lease time specified.\n", __FUNCTION__);
    return false;
  }

  // No DNS -> DNS will not be sent to client.
  return true;
}

bool DhcpServerImpl::CreateSocket() {
  // Create new UDP socket
  socket_ = SharedFD::Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (!socket_->IsOpen()) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to create socket (%d:%s).\n",
               __FUNCTION__, socket_->GetErrno(), socket_->StrError());
    return false;
  }

  // Allow multiple sockets to reuse the (broadcast) address.
  const int flag_enable = true;
  if (socket_->SetSockOpt(
          SOL_SOCKET, SO_REUSEADDR, &flag_enable, sizeof(flag_enable)) == -1) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to enable reuseaddr on socket (%d:%s)\n",
               __FUNCTION__, socket_->GetErrno(), socket_->StrError());
    return false;
  }

  // Enable socket to send broadcast messages.
  if (socket_->SetSockOpt(
          SOL_SOCKET, SO_BROADCAST, &flag_enable, sizeof(flag_enable)) == -1) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to enable broadcasts on socket (%d:%s)\n",
               __FUNCTION__, socket_->GetErrno(), socket_->StrError());
    return false;
  }

  // Bind socket to specific interface.
  if (socket_->SetSockOpt(
          SOL_SOCKET, SO_BINDTODEVICE, bind_device_.c_str(),
          bind_device_.length()) == -1) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to bind socket to device %s (%d:%s)\n",
               __FUNCTION__, bind_device_.c_str(), socket_->GetErrno(),
               socket_->StrError());
    return false;
  }

  // Open socket.
  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = kDhcpBroadcastAddress;
  address.sin_port = htons(kDhcpServerPort);
  if (socket_->Bind(
          reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    KLOG_ERROR(LOG_TAG,
               "%s: Failed to start listening for broadcasts on %s (%d:%s).\n",
               __FUNCTION__, bind_device_.c_str(), socket_->GetErrno(),
               socket_->StrError());
    return false;
  }

  return true;
}

in_addr_t DhcpServerImpl::HWAdrdessToIPAddress(
    const std::vector<uint8_t>& address) {
  std::string hw_address(address.begin(), address.end());
  in_addr_t ip_address = 0;

  // Find first available address, preferably one assigned to the client.
  std::map<std::string, in_addr_t>::iterator iter;
  iter = address_map_.find(hw_address);
  if (iter != address_map_.end()) {
    ip_address = iter->second;
  }

  // No existing entry found. Find first available element.
  // TODO(ender): add expiration.
  if (!ip_address) {
    std::vector<bool> addresses_in_use(end_ip_address_ - start_ip_address_);
    for (iter = address_map_.begin(); iter != address_map_.end(); ++iter) {
      in_addr_t addr = iter->second;
      if (addr >= start_ip_address_ && addr <= end_ip_address_) {
        addresses_in_use[addr - start_ip_address_] = true;
      } else {
        KLOG_ERROR(LOG_TAG,
                   "%s: Found invalid IP address assignment %d.%d.%d.%d\n",
                   __FUNCTION__,
                   addr >> 24, (addr >> 16) & 0xff,
                   (addr >> 8) & 0xff, addr & 0xff);
      }
    }

    size_t index = 0;
    for (index = 0; index < addresses_in_use.size(); ++index) {
      if (!addresses_in_use[index]) break;
    }

    // Do we have at least one available IP address?
    if (index == addresses_in_use.size()) {
      KLOG_ERROR(LOG_TAG, "%s: Address pool exhausted!\n", __FUNCTION__);
      return 0;
    }

    // First available IP address is at offset |index|.
    // Insert this address to a map of HW to IP addresses.
    ip_address = start_ip_address_ + index;
    KLOG_INFO(LOG_TAG, "%s: Assigning IP address %d.%d.%d.%d.\n",
              __FUNCTION__,
              ip_address >> 24, (ip_address >> 16) & 0xff,
              (ip_address >> 8) & 0xff, ip_address & 0xff);
    address_map_.insert(
        std::pair<std::string, in_addr_t>(hw_address, ip_address));
  }

  return ip_address;
}

DhcpMessage* DhcpServerImpl::BuildResponse(DhcpMessage* request) {
  DhcpMessage::MessageType type = DhcpMessage::kDhcpIgnore;

  switch (request->GetMessageType()) {
    case DhcpMessage::kDhcpDiscover:
      type = DhcpMessage::kDhcpOffer;
      break;

    case DhcpMessage::kDhcpRequest:
      type = DhcpMessage::kDhcpAck;
      break;

    default:
      return NULL;
      break;
  }

  in_addr_t ip_address = HWAdrdessToIPAddress(request->GetClientHWAddress());
  if (!ip_address) {
    return NULL;
  }

  std::unique_ptr<DhcpMessage> response(DhcpMessage::New());
  response->InitializeFrom(*request);
  response->SetMessageType(type);
  response->SetServerIpAddress(server_ip_address_);
  response->SetAssignedIpAddress(ip_address);
  response->SetNetworkMask(network_mask_);
  response->SetGatewayIpAddress(gateway_ip_address_);
  response->SetLeaseTimeSeconds(lease_time_seconds_);
  response->SetBroadcastIpAddress(start_ip_address_ | ~network_mask_);
  response->SetDnsIpAddress(dns_address_);
  response->SetMtu(mtu_);

  return response.release();
}

bool DhcpServerImpl::ReceiveDHCPMessage(std::vector<uint8_t>* data) {
  int bytes;
  sockaddr_in client_addr;
  socklen_t length = sizeof(client_addr);

  // Resize constructs new data if the target capacity is larger than the
  // source. This means that if we 'grow' size after we read data from socket,
  // our data would be overwritten with \0.
  // That does not happen if we grow it beforehand, and shrink it after.
  data->resize(kDhcpMessageMaxLength);

  bytes = socket_->RecvFrom(
      &data->front(), data->size(), 0,
      reinterpret_cast<sockaddr*>(&client_addr), &length);

  if (bytes < 0) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to read from socket: %d (%s).\n",
               __FUNCTION__, socket_->GetErrno(), socket_->StrError());
    return false;
  }

  data->resize(bytes);
  return true;
}

void DhcpServerImpl::SendDHCPMessage(const std::vector<uint8_t>& data) {
  sockaddr_in dest_addr;

  dest_addr.sin_family = AF_INET;
  dest_addr.sin_addr.s_addr = kDhcpBroadcastAddress;
  dest_addr.sin_port = htons(kDhcpClientPort);

  if (socket_->SendTo(&data.front(), data.size(), 0,
                      reinterpret_cast<sockaddr*>(&dest_addr),
                      sizeof(dest_addr)) < 0) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to send DHCP response: %d(%s).\n",
               __FUNCTION__, socket_->GetErrno(), socket_->StrError());
  }
}

bool DhcpServerImpl::Start(const DhcpServer::Options& options) {
  if (!ProcessOptions(options)) return false;
  if (!CreateSocket()) return false;

  std::vector<uint8_t> message(kDhcpMessageMaxLength, 0);

  while (ReceiveDHCPMessage(&message)) {
    // De-serialize the DHCP request message.
    std::unique_ptr<DhcpMessage> request(DhcpMessage::New());
    if (!request->Deserialize(message)) {
      continue;
    }

    // Build a DHCP response based on the request.
    std::unique_ptr<DhcpMessage> response(BuildResponse(request.get()));
    if (!response.get()) {
      continue;
    }

    // "Clear" the vector, but don't re-allocate.
    message.resize(0);
    response->Serialize(&message);

    SendDHCPMessage(message);
  }

  return true;
}

}  // namespace

DhcpServer* DhcpServer::New() {
  return new DhcpServerImpl();
}

}  // namespace avd

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
#include "dhcp_message.h"

#include "logging.h"

namespace avd {
namespace {
// To learn more about option ids and data navigate to:
// We're only implementing a subset.
// http://www.iana.org/assignments/bootp-dhcp-parameters/bootp-dhcp-parameters.xhtml
// or
// http://www.networksorcery.com/enp/rfc/rfc1533.txt
enum DhcpOptionIds {
  kDhcpOptionIdPad = 0,
  kDhcpOptionSubnetMask = 1,
  kDhcpOptionGatewayAddress = 3,
  kDhcpOptionNameServer = 6,
  kDhcpOptionMtu = 26,
  kDhcpOptionBroadcastAddress = 28,
  kDhcpOptionLeaseTime = 51,
  kDhcpOptionMessageType = 53,
  kDhcpOptionDhcpServer = 54,
  kDhcpOptionParameterRequestList = 55,
  kDhcpOptionMaxMessageSize = 57,
  kDhcpOptionRenewalTime = 58,
  kDhcpOptionRebindTime = 59,
  kDhcpOptionClassIdentifier = 60,
  kDhcpOptionIdEnd = 255,
};

const uint32_t kDhcpOptionMagicCookie = 0x63825363;
const uint32_t kHwMaxAddressLength = 16;
const uint32_t kHwMacAddressLength = 6;

// DHCP message representation.
class DhcpMessageImpl : public DhcpMessage {
 public:
  DhcpMessageImpl();
  virtual ~DhcpMessageImpl();

  // Decompose DHCP message from buffer.
  virtual bool Deserialize(const std::vector<uint8_t>& data);

  // Compose DHCP message.
  virtual bool Serialize(std::vector<uint8_t>* data) const;

  // Decompose option fields
  bool DeserializeOptions(const std::vector<uint8_t>& data, size_t offset);

  virtual bool InitializeFrom(const DhcpMessage& request);

  // Get / Set DHCP message type.
  virtual MessageType GetMessageType() const {
    return MessageType(command_);
  }

  virtual void SetMessageType(MessageType type) {
    command_ = type;
  }

  virtual bool IsBroadcast() const {
    return is_broadcast_;
  }

  virtual bool IsRequest() const {
    return is_request_;
  }

  virtual in_addr_t GetServerIpAddress() const {
    return server_ip_address_;
  }

  virtual void SetServerIpAddress(in_addr_t address) {
    server_ip_address_ = address;
  }

  // Get / Set client IP address.
  virtual in_addr_t GetClientIpAddress() const {
    return client_current_ip_address_;
  }

  virtual void SetClientIpAddress(in_addr_t address) {
    client_current_ip_address_ = address;
  }

  virtual in_addr_t GetAssignedIpAddress() const {
    return client_assigned_ip_address_;
  }

  virtual void SetAssignedIpAddress(in_addr_t address) {
    client_assigned_ip_address_ = address;
  }

  // Get / Set network mask.
  virtual in_addr_t GetNetworkMask() const {
    return network_mask_;
  }

  virtual void SetNetworkMask(in_addr_t mask) {
    network_mask_ = mask;
  }

  // Get / Set gateway IP address.
  virtual in_addr_t GetGatewayIpAddress() const {
    return gateway_ip_address_;
  }

  virtual void SetGatewayIpAddress(in_addr_t address) {
    gateway_ip_address_ = address;
  }

  virtual in_addr_t GetBroadcastIpAddress() const {
    return broadcast_ip_address_;
  }

  virtual void SetBroadcastIpAddress(in_addr_t address) {
    broadcast_ip_address_ = address;
  }

  virtual in_addr_t GetDnsIpAddress() const {
    return dns_ip_address_;
  }

  virtual void SetDnsIpAddress(in_addr_t address) {
    dns_ip_address_ = address;
  }

  // Get / Set lease time.
  virtual uint32_t GetLeaseTimeSeconds() const {
    return lease_time_;
  }

  virtual void SetLeaseTimeSeconds(uint32_t lease_time) {
    lease_time_ = lease_time;
  }

  virtual int32_t GetMtu() const {
    return mtu_;
  }

  virtual void SetMtu(int32_t mtu) {
    mtu_ = mtu;
  }

  virtual uint32_t GetTransactionId() const {
    return transaction_id_;
  }

  virtual const std::vector<uint8_t>& GetClientHWAddress() const;

 private:
  bool is_request_;
  uint32_t command_;
  uint32_t transaction_id_;
  bool is_broadcast_;
  uint32_t server_ip_address_;
  uint32_t client_current_ip_address_;
  uint32_t client_assigned_ip_address_;
  uint32_t gateway_ip_address_;
  uint32_t broadcast_ip_address_;
  uint32_t dns_ip_address_;
  uint32_t network_mask_;
  uint32_t lease_time_;
  int32_t mtu_;
  std::vector<uint8_t> client_hw_address_;
};

DhcpMessageImpl::DhcpMessageImpl()
    : is_request_(false),
      command_(0),
      transaction_id_(0),
      is_broadcast_(false),
      server_ip_address_(0),
      client_current_ip_address_(0),
      client_assigned_ip_address_(0),
      gateway_ip_address_(0),
      broadcast_ip_address_(0),
      dns_ip_address_(0),
      network_mask_(0),
      lease_time_(0),
      mtu_(0) {}

DhcpMessageImpl::~DhcpMessageImpl() {}

bool DhcpMessageImpl::Deserialize(const std::vector<uint8_t>& data) {
  size_t offset = 0;
  uint32_t temp;

  // Command: 1 == request, 2 == response. We accept requests only.
  if (!ConsumeInt(data, &offset, &temp, 1) || temp != 1) return false;
  is_request_ = true;
  // Hardware address type. 1 == ethernet.
  if (!ConsumeInt(data, &offset, &temp, 1) || temp != 1) return false;
  // Hardware address length. sizeof(MAC) for ethernet.
  if (!ConsumeInt(data, &offset, &temp, 1) || temp != kHwMacAddressLength)
    return false;
  // Used for booting via relay. Ignore.
  if (!SkipBytes(data, &offset, 1)) return false;
  // Client transaction ID. Used to match answer with request.
  if (!ConsumeInt(data, &offset, &transaction_id_, 4)) return false;
  // Client reporting ellapsed time since last request. Ignore.
  if (!SkipBytes(data, &offset, 2)) return false;
  // Client flags, currently only specifies broadcast message.
  // When broadcast is specified, server should respond with broadcast response.
  if (!ConsumeInt(data, &offset, &temp, 2)) return false;
  is_broadcast_ = (temp & 0x8000);
  // Client's current IP address. We can use it to give it back to client.
  if (!ConsumeInt(data, &offset, &client_current_ip_address_, 4)) return false;
  client_assigned_ip_address_ = client_current_ip_address_;
  // Client's new IP address. Ignore.
  if (!SkipBytes(data, &offset, 4)) return false;
  // Our IP address. Ignore.
  if (!SkipBytes(data, &offset, 4)) return false;
  // Relay server IP address. Ignore.
  if (!SkipBytes(data, &offset, 4)) return false;
  // Client MAC address (or other HW address).
  // Hardware address can take up to 16 bytes. we consume it all, but resize to
  // length of hardware address right after.
  client_hw_address_.resize(kHwMaxAddressLength);
  if (!ConsumeBytes(
      data, &offset, &client_hw_address_.front(), client_hw_address_.size())) {
    return false;
  }
  client_hw_address_.resize(kHwMacAddressLength);

  // Server host name. Ignore.
  if (!SkipBytes(data, &offset, 64)) return false;
  // BootP file name. Ignore.
  if (!SkipBytes(data, &offset, 128)) return false;

  // Potentially -- options.
  uint32_t magic_cookie = 0;
  if (ConsumeInt(data, &offset, &magic_cookie, 4) &&
      magic_cookie == kDhcpOptionMagicCookie) {
    return DeserializeOptions(data, offset);
  }

  return true;
}

bool DhcpMessageImpl::Serialize(std::vector<uint8_t>* data) const {
  if (!data) return false;
  data->clear();
  // 1 == request, 2 == response
  AppendInt(data, is_request_ ? 1 : 2, 1);
  // Hardware type == ethernet (1).
  AppendInt(data, 1, 1);
  // Hardware address length == sizeof(MAC).
  AppendInt(data, kHwMacAddressLength, 1);
  // Pad data.
  AppendInt(data, 0, 1);
  // Transaction ID.
  AppendInt(data, transaction_id_, 4);
  // Time ellapsed. 0.
  AppendInt(data, 0, 2);
  // Flags (only flag possible is broadcast).
  AppendInt(data, is_broadcast_ ? 0x8000 : 0, 2);
  // Client IP address.
  AppendInt(data, client_current_ip_address_, 4);
  AppendInt(data, client_assigned_ip_address_, 4);
  // Our IP address.
  AppendInt(data, server_ip_address_, 4);
  AppendInt(data, 0, 4);
  AppendBytes(data, &client_hw_address_.front(), client_hw_address_.size());
  if (client_hw_address_.size() < kHwMaxAddressLength) {
    PadBytes(data, kHwMaxAddressLength - client_hw_address_.size());
  }
  // Server name. Ignore.
  PadBytes(data, 64);
  // TFTP Boot file path. Ignore
  PadBytes(data, 128);

  // MAGIC starts here. Writing options.
  AppendInt(data, kDhcpOptionMagicCookie, 4);

  // Message type
  AppendInt(data, kDhcpOptionMessageType, 1);
  AppendInt(data, 1, 1);
  AppendInt(data, command_, 1);

  // Self address
  // While this is an _optional_ option, that may, but doesn't have to be
  // reported by the router, and is used to differentiate between multiple DHCP
  // servers, Android's new DHCP client immediately crashes if it's not
  // specified.
  AppendInt(data, kDhcpOptionDhcpServer, 1);
  AppendInt(data, 4, 1);
  AppendInt(data, server_ip_address_, 4);

  // MTU
  AppendInt(data, kDhcpOptionMtu, 1);
  AppendInt(data, 2, 1);
  AppendInt(data, mtu_, 2);

  // DNS
  AppendInt(data, kDhcpOptionNameServer, 1);
  AppendInt(data, 4, 1);
  AppendInt(data, dns_ip_address_, 4);

  // Network mask
  AppendInt(data, kDhcpOptionSubnetMask, 1);
  AppendInt(data, 4, 1);
  AppendInt(data, network_mask_, 4);

  // Gateway IP
  AppendInt(data, kDhcpOptionGatewayAddress, 1);
  AppendInt(data, 4, 1);
  AppendInt(data, gateway_ip_address_, 4);

  // Broadcast address
  AppendInt(data, kDhcpOptionBroadcastAddress, 1);
  AppendInt(data, 4, 1);
  AppendInt(data, broadcast_ip_address_, 4);

  // Lease time
  AppendInt(data, kDhcpOptionLeaseTime, 1);
  AppendInt(data, 4, 1);
  AppendInt(data, lease_time_, 4);

  // Renewal time must be much shorter than lease time.
  // It tells client where to re-new their IP address.
  AppendInt(data, kDhcpOptionRenewalTime, 1);
  AppendInt(data, 4, 1);

  if (lease_time_ == kLeaseTimeInfinite) {
    // ~0 means indefinitely. No renewal time.
    AppendInt(data, lease_time_, 4);
  } else {
    AppendInt(data, lease_time_ - 30, 4);
  }

  // Rebind time must be shorter than lease time.
  // It tells client to restart DHCP binding.
  AppendInt(data, kDhcpOptionRebindTime, 1);
  AppendInt(data, 4, 1);

  if (lease_time_ == kLeaseTimeInfinite) {
    AppendInt(data, lease_time_, 4);
  } else {
    AppendInt(data, lease_time_ - 10, 4);
  }

  return true;
}

bool DhcpMessageImpl::InitializeFrom(const DhcpMessage& request) {
  if (!request.IsRequest()) return false;

  is_request_ = false;
  transaction_id_ = request.GetTransactionId();
  is_broadcast_ = request.IsBroadcast();
  client_current_ip_address_ = request.GetClientIpAddress();
  client_hw_address_ = request.GetClientHWAddress();

  return true;
}

const std::vector<uint8_t>& DhcpMessageImpl::GetClientHWAddress() const {
  return client_hw_address_;
}

bool DhcpMessageImpl::DeserializeOptions(
    const std::vector<uint8_t>& data, size_t offset) {
  // Structure of each option is:
  // - option type (1 byte)
  // - option length (1 byte)
  // - option data (|option length| bytes)
  // We want to make sure we have at least first two fields available, hence
  // the unusual condition.
  // Option parameter list ends with option 255.
  //
  // Note: this is a dummy implementation. We generally toss away whatever the
  // client is sending our way currently. This method validates the DHCP
  // request.
  while ((offset + 1) < data.size() && data[offset] != kDhcpOptionIdEnd) {
    uint8_t option_id = data[offset++];

    // Special option-id: pad data.
    if (option_id == kDhcpOptionIdPad) continue;

    uint8_t option_length = data[offset++];
    KLOG_DEBUG(LOG_TAG, "%s: DHCP Option %d, size %d\n",
               __FUNCTION__, option_id, option_length);

    if (option_id == kDhcpOptionMessageType && option_length == 1) {
      if (!ConsumeInt(data, &offset, &command_, 1)) {
        KLOG_ERROR(LOG_TAG, "%s: Could not read DHCP request type.\n",
                   __FUNCTION__);
        return false;
      }
    } else {
      if (!SkipBytes(data, &offset, option_length)) {
        KLOG_ERROR(LOG_TAG, "%s: Malformed DHCP option %d at %zu/%zu. "
                   "Ignoring request.\n",
                   __FUNCTION__, option_id, offset, data.size());
        return false;
      }
    }
  }

  return true;
}

}  // namespace

DhcpMessage* DhcpMessage::New() {
  return new DhcpMessageImpl();
}

}  // namespace avd

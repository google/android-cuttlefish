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
#ifndef DHCP_MESSAGE_H_
#define DHCP_MESSAGE_H_

#include <arpa/inet.h>

#include <cstdint>
#include <vector>

#include "serializable.h"

namespace avd {

// Abstraction of the DHCP message.
class DhcpMessage : public Serializable {
 public:
  enum MessageType {
    kDhcpIgnore = 0,
    kDhcpDiscover = 1,
    kDhcpOffer = 2,
    kDhcpRequest = 3,
    kDhcpDecline = 4,
    kDhcpAck = 5,
    kDhcpNak = 6,
    kDhcpRelease = 7,
    kDhcpInform = 8,
  };

  static const uint32_t kLeaseTimeInfinite = ~0;

  DhcpMessage() {}
  virtual ~DhcpMessage() {}

  // Build new default instance of DhcpMessage.
  static DhcpMessage* New();

  // Decompose DHCP message from buffer.
  virtual bool Deserialize(const std::vector<uint8_t>& data) = 0;

  // Compose DHCP message.
  virtual bool Serialize(std::vector<uint8_t>* data) const = 0;

  // Initialize response message from request.
  virtual bool InitializeFrom(const DhcpMessage& request) = 0;

  // Get / Set DHCP message type.
  virtual MessageType GetMessageType() const = 0;
  virtual void SetMessageType(MessageType t) = 0;

  // Get / Set DHCP server IP address.
  virtual in_addr_t GetServerIpAddress() const = 0;
  virtual void SetServerIpAddress(in_addr_t address) = 0;

  // Get / Set client IP address.
  virtual in_addr_t GetClientIpAddress() const = 0;
  virtual void SetClientIpAddress(in_addr_t address) = 0;

  // Get / Set client assigned IP address.
  virtual in_addr_t GetAssignedIpAddress() const = 0;
  virtual void SetAssignedIpAddress(in_addr_t address) = 0;

  // Get / Set network mask.
  virtual in_addr_t GetNetworkMask() const = 0;
  virtual void SetNetworkMask(in_addr_t address) = 0;

  // Get / Set gateway IP address.
  virtual in_addr_t GetGatewayIpAddress() const = 0;
  virtual void SetGatewayIpAddress(in_addr_t address) = 0;

  // Get / Set broadcast IP address.
  virtual in_addr_t GetBroadcastIpAddress() const = 0;
  virtual void SetBroadcastIpAddress(in_addr_t address) = 0;

  // Get / Set DNS IP address.
  virtual in_addr_t GetDnsIpAddress() const = 0;
  virtual void SetDnsIpAddress(in_addr_t address) = 0;

  // Get / Set lease time.
  virtual uint32_t GetLeaseTimeSeconds() const = 0;
  virtual void SetLeaseTimeSeconds(uint32_t time_seconds) = 0;

  // Get / Set Max Transfer Unit;
  virtual int32_t GetMtu() const = 0;
  virtual void SetMtu(int32_t mtu) = 0;

  // Get Client Hardware address.
  // Hardware address will be placed in |target| memory.
  // No more than |length| bytes will be stored. If |length| is shorter than
  // actual hardware address, data will be truncated.
  // Returns actual length of hardware address.
  virtual const std::vector<uint8_t>& GetClientHWAddress() const = 0;

  // Get Serial / Transaction ID as specified by client.
  virtual uint32_t GetTransactionId() const = 0;

  // Flags
  virtual bool IsRequest() const = 0;
  virtual bool IsBroadcast() const = 0;

 private:
  DhcpMessage(const DhcpMessage&);
  DhcpMessage& operator= (const DhcpMessage&);
};

}  // namespace avd

#endif  // DHCP_MESSAGE_H_

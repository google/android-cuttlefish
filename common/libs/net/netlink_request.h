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
#ifndef COMMON_LIBS_NET_NETLINK_REQUEST_H_
#define COMMON_LIBS_NET_NETLINK_REQUEST_H_

#include <stddef.h>
#include <memory>
#include <string>

namespace avd {
// Abstraction of Network link request.
// Used to supply kernel with information about which interface needs to be
// changed, and how.
class NetlinkRequest {
 public:
  NetlinkRequest() {}
  virtual ~NetlinkRequest() {}

  // Add an IFLA tag followed by a string.
  // Returns true, if successful.
  virtual void AddString(uint16_t type, const std::string& value) = 0;

  // Add an IFLA tag followed by int32.
  // Returns true, if successful.
  virtual void AddInt32(uint16_t type, int32_t value) = 0;

  // Add an IFLA tag followed by int8.
  // Returns true, if successful.
  virtual void AddInt8(uint16_t type, int8_t value) = 0;

  // Add an interface info structure.
  // Parameter |if_index| specifies particular interface index to which the
  // attributes following the IfInfo apply.
  virtual void AddIfInfo(int32_t if_index, bool is_operational) = 0;

  // Add an address info to a specific interface.
  // This method assumes the prefix length for address info is 24.
  virtual void AddAddrInfo(int32_t if_index) = 0;

  // Creates new list.
  // List mimmic recursive structures in a flat, contiuous representation.
  // Each call to PushList() should have a corresponding call to PopList
  // indicating end of sub-attribute list.
  virtual void PushList(uint16_t type) = 0;

  // Indicates end of previously declared list.
  virtual void PopList() = 0;

  // Request data.
  virtual void* RequestData() = 0;

  // Request length.
  virtual size_t RequestLength() = 0;

  // Set Sequence Number.
  virtual void SetSeqNo(uint32_t seq_no) = 0;

  // Request Sequence Number.
  virtual uint32_t SeqNo() = 0;

  // Append raw data to buffer.
  // data must not be null.
  // Returns pointer to copied location.
  virtual void* AppendRaw(const void* data, size_t length) = 0;

  // Reserve |length| number of bytes in the buffer.
  // Returns pointer to reserved location.
  virtual void* ReserveRaw(size_t length) = 0;

  // Append specialized data.
  template <typename T> T* Append(const T& data) {
    return static_cast<T*>(AppendRaw(&data, sizeof(T)));
  }

  // Reserve specialized data.
  template <typename T> T* Reserve() {
    return static_cast<T*>(ReserveRaw(sizeof(T)));
  }

  // Create new Netlink Request structure.
  // Parameter |type| specifies netlink request type (eg. RTM_NEWLINK), while
  // |flags| are netlink and request specific flags (eg. NLM_F_DUMP).
  static std::unique_ptr<NetlinkRequest> New(int type, int flags);

 private:
  NetlinkRequest(const NetlinkRequest&);
  NetlinkRequest& operator= (const NetlinkRequest&);
};
}  // namespace avd
#endif  // COMMON_LIBS_NET_NETLINK_REQUEST_H_

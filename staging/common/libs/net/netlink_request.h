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

#include <linux/netlink.h>
#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

namespace cvd {
// Abstraction of Network link request.
// Used to supply kernel with information about which interface needs to be
// changed, and how.
class NetlinkRequest {
 public:
  // Create new Netlink Request structure.
  // Parameter |type| specifies netlink request type (eg. RTM_NEWLINK), while
  // |flags| are netlink and request specific flags (eg. NLM_F_DUMP).
  NetlinkRequest(int type, int flags);
  NetlinkRequest(NetlinkRequest&& other);

  ~NetlinkRequest() = default;

  // Add an IFLA tag followed by a string.
  // Returns true, if successful.
  void AddString(uint16_t type, const std::string& value);

  // Add an IFLA tag followed by an integer.
  template <typename T>
  void AddInt(uint16_t type, T value) {
    static_assert(std::is_integral<T>::value,
                  "AddInt must be used on integer types.");
    AppendTag(type, &value, sizeof(value));
  }

  // Add an interface info structure.
  // Parameter |if_index| specifies particular interface index to which the
  // attributes following the IfInfo apply.
  void AddIfInfo(int32_t if_index, bool is_operational);

  // Add an address info to a specific interface.
  void AddAddrInfo(int32_t if_index, int prefix_len = 24);

  // Creates new list.
  // List mimmic recursive structures in a flat, contiuous representation.
  // Each call to PushList() should have a corresponding call to PopList
  // indicating end of sub-attribute list.
  void PushList(uint16_t type);

  // Indicates end of previously declared list.
  void PopList();

  // Request data.
  void* RequestData() const;

  // Request length.
  size_t RequestLength() const;

  // Request Sequence Number.
  uint32_t SeqNo() const;

  // Append raw data to buffer.
  // data must not be null.
  // Returns pointer to copied location.
  void* AppendRaw(const void* data, size_t length);

  // Reserve |length| number of bytes in the buffer.
  // Returns pointer to reserved location.
  void* ReserveRaw(size_t length);

  // Append specialized data.
  template <typename T> T* Append(const T& data) {
    return static_cast<T*>(AppendRaw(&data, sizeof(T)));
  }

  // Reserve specialized data.
  template <typename T> T* Reserve() {
    return static_cast<T*>(ReserveRaw(sizeof(T)));
  }

 private:
  nlattr* AppendTag(uint16_t type, const void* data, uint16_t length);

  std::vector<std::pair<nlattr*, int32_t>> lists_;
  std::vector<char> request_;
  nlmsghdr* header_;

  NetlinkRequest(const NetlinkRequest&) = delete;
  NetlinkRequest& operator= (const NetlinkRequest&) = delete;
};
}  // namespace cvd
#endif  // COMMON_LIBS_NET_NETLINK_REQUEST_H_

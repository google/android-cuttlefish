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
#include "common/libs/net/netlink_request.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "android-base/logging.h"

namespace cuttlefish {
namespace {
uint32_t kRequestSequenceNumber = 0;
}  // namespace

uint32_t NetlinkRequest::SeqNo() const {
  return header_->nlmsg_seq;
}

void* NetlinkRequest::AppendRaw(const void* data, size_t length) {
  auto* output = static_cast<char*>(ReserveRaw(length));
  const auto* input = static_cast<const char*>(data);
  std::copy(input, input + length, output);
  return output;
}

void* NetlinkRequest::ReserveRaw(size_t length) {
  size_t original_size = request_.size();
  request_.resize(original_size + RTA_ALIGN(length), '\0');
  return reinterpret_cast<void*>(request_.data() + original_size);
}

nlattr* NetlinkRequest::AppendTag(
    uint16_t type, const void* data, uint16_t data_length) {
  nlattr* attr = Reserve<nlattr>();
  attr->nla_type = type;
  attr->nla_len = RTA_LENGTH(data_length);
  AppendRaw(data, data_length);
  return attr;
}

NetlinkRequest::NetlinkRequest(int32_t command, int32_t flags) {
  request_.reserve(512);
  header_ = Reserve<nlmsghdr>();
  flags |= NLM_F_ACK | NLM_F_REQUEST;
  header_->nlmsg_flags = flags;
  header_->nlmsg_type = command;
  header_->nlmsg_pid = getpid();
  header_->nlmsg_seq = kRequestSequenceNumber++;
}

NetlinkRequest::NetlinkRequest(NetlinkRequest&& other) {
  using std::swap;
  swap(lists_, other.lists_);
  swap(header_, other.header_);
  swap(request_, other.request_);
}

void NetlinkRequest::AddString(uint16_t type, const std::string& value) {
  AppendTag(type, value.c_str(), value.length() + 1);
}

void NetlinkRequest::AddIfInfo(int32_t if_index, bool operational) {
  ifinfomsg* if_info = Reserve<ifinfomsg>();
  if_info->ifi_family = AF_UNSPEC;
  if_info->ifi_index = if_index;
  if_info->ifi_flags = operational ? IFF_UP : 0;
  if_info->ifi_change = IFF_UP;
}

void NetlinkRequest::AddAddrInfo(int32_t if_index, int prefix_len) {
  ifaddrmsg* ad_info = Reserve<ifaddrmsg>();
  ad_info->ifa_family = AF_INET;
  ad_info->ifa_prefixlen = prefix_len;
  ad_info->ifa_flags = IFA_F_PERMANENT | IFA_F_SECONDARY;
  ad_info->ifa_scope = 0;
  ad_info->ifa_index = if_index;
}

void NetlinkRequest::AddMacAddress(const std::array<unsigned char, 6>& address) {
  AppendTag(IFLA_ADDRESS, address.data(), 6);
}

void NetlinkRequest::PushList(uint16_t type) {
  int length = request_.size();
  nlattr* list = AppendTag(type, NULL, 0);
  lists_.push_back(std::make_pair(list, length));
}

void NetlinkRequest::PopList() {
  if (lists_.empty()) {
    LOG(ERROR) << "List pop with no lists left on stack.";
    return;
  }

  std::pair<nlattr*, int> list = lists_.back();
  lists_.pop_back();
  list.first->nla_len = request_.size() - list.second;
}

void* NetlinkRequest::RequestData() const {
  // Update request length before reporting raw data.
  header_->nlmsg_len = request_.size();
  return header_;
}

size_t NetlinkRequest::RequestLength() const {
  return request_.size();
}

}  // namespace cuttlefish

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

#include <string>
#include <vector>

#include "common/libs/glog/logging.h"

namespace avd {
namespace {
// Representation of Network link request. Used to supply kernel with
// information about which interface needs to be changed, and how.
class NetlinkRequestImpl : public NetlinkRequest {
 public:
  NetlinkRequestImpl(int32_t command, int32_t flags);

  virtual void AddString(uint16_t type, const std::string& value);
  virtual void AddInt32(uint16_t type, int32_t value);
  virtual void AddInt8(uint16_t type, int8_t value);
  virtual void AddAddrInfo(int32_t if_index);
  virtual void AddIfInfo(int32_t if_index, bool operational);
  virtual void PushList(uint16_t type);
  virtual void PopList();
  virtual void* RequestData();
  virtual size_t RequestLength();
  virtual uint32_t SeqNo() {
    return header_->nlmsg_seq;
  }
  virtual void SetSeqNo(uint32_t seq_no) {
    header_->nlmsg_seq = seq_no;
  }

  void* AppendRaw(const void* data, size_t length) {
    return request_.AppendRaw(data, length);
  }

 private:
  class RequestBuffer {
   public:
    RequestBuffer()
        : current_(0),
          buffer_length_(512),
          buffer_(new uint8_t[buffer_length_]) {}

    ~RequestBuffer() {
      delete[] buffer_;
    }

    void* AppendRaw(const void* data, size_t length) {
      // Replace old buffer with new one. This is not thread safe (and does not
      // have to be).
      if (length > (buffer_length_ - current_)) {
        uint8_t* new_buffer = new uint8_t[buffer_length_ * 2];
        memcpy(new_buffer, buffer_, buffer_length_);
        delete[] buffer_;

        buffer_length_ *= 2;
        buffer_ = new_buffer;
      }

      uint8_t* out = &buffer_[current_];

      if (data) {
        memcpy(out, data, length);
      } else {
        memset(out, 0, length);
      }
      // Pad with zeroes until buffer size is aligned.
      memset(&out[length], 0, RTA_ALIGN(length) - length);
      current_ += RTA_ALIGN(length);
      return out;
    }

    size_t Length() {
      return current_;
    }

   private:
    size_t   current_;
    size_t   buffer_length_;
    uint8_t* buffer_;
  };

  nlattr* AppendTag(uint16_t type, const void* data, uint16_t length);

  std::vector<std::pair<nlattr*, int32_t> > lists_;
  RequestBuffer request_;
  nlmsghdr* header_;
};

nlattr* NetlinkRequestImpl::AppendTag(
    uint16_t type, const void* data, uint16_t data_length) {
  nlattr* attr = Reserve<nlattr>();
  attr->nla_type = type;
  attr->nla_len = RTA_LENGTH(data_length);
  AppendRaw(data, data_length);
  return attr;
}

NetlinkRequestImpl::NetlinkRequestImpl(
    int32_t command, int32_t flags)
    : header_(Reserve<nlmsghdr>()) {
  header_->nlmsg_flags = flags;
  header_->nlmsg_type = command;
}

void NetlinkRequestImpl::AddString(uint16_t type, const std::string& value) {
  AppendTag(type, value.c_str(), value.length());
}

void NetlinkRequestImpl::AddInt32(uint16_t type, int32_t value) {
  AppendTag(type, &value, sizeof(value));
}

void NetlinkRequestImpl::AddInt8(uint16_t type, int8_t value) {
  AppendTag(type, &value, sizeof(value));
}

void NetlinkRequestImpl::AddIfInfo(int32_t if_index, bool operational) {
  ifinfomsg* if_info = Reserve<ifinfomsg>();
  if_info->ifi_family = AF_UNSPEC;
  if_info->ifi_index = if_index;
  if_info->ifi_flags = operational ? IFF_UP : 0;
  if_info->ifi_change = IFF_UP;
}

void NetlinkRequestImpl::AddAddrInfo(int32_t if_index) {
  ifaddrmsg* ad_info = Reserve<ifaddrmsg>();
  ad_info->ifa_family = AF_INET;
  ad_info->ifa_prefixlen = 24;
  ad_info->ifa_flags = IFA_F_PERMANENT | IFA_F_SECONDARY;
  ad_info->ifa_scope = 0;
  ad_info->ifa_index = if_index;
}

void NetlinkRequestImpl::PushList(uint16_t type) {
  int length = request_.Length();
  nlattr* list = AppendTag(type, NULL, 0);
  lists_.push_back(std::make_pair(list, length));
}

void NetlinkRequestImpl::PopList() {
  if (lists_.empty()) {
    LOG(ERROR) << "List pop with no lists left on stack.";
    return;
  }

  std::pair<nlattr*, int> list = lists_.back();
  lists_.pop_back();
  list.first->nla_len = request_.Length() - list.second;
}

void* NetlinkRequestImpl::RequestData() {
  // Update request length before reporting raw data.
  header_->nlmsg_len = request_.Length();
  return header_;
}

size_t NetlinkRequestImpl::RequestLength() {
  return request_.Length();
}
}  // namespace

std::unique_ptr<NetlinkRequest> NetlinkRequest::New(
    int type, int flags) {
  // Ensure we receive response.
  flags |= NLM_F_ACK | NLM_F_REQUEST;

  return std::unique_ptr<NetlinkRequest>(new NetlinkRequestImpl(
      type, flags));
}

}  // namespace avd

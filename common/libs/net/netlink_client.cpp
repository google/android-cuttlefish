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
#include "common/libs/net/netlink_client.h"

#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"
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

    // Append data to buffer. If |data| is NULL, erase |length| bytes instead.
    void Append(const void* data, size_t length) {
      // Replace old buffer with new one. This is not thread safe (and does not
      // have to be).
      if (length > (buffer_length_ - current_)) {
        uint8_t* new_buffer = new uint8_t[buffer_length_ * 2];
        memcpy(new_buffer, buffer_, buffer_length_);
        delete[] buffer_;

        buffer_length_ *= 2;
        buffer_ = new_buffer;
      }

      if (data) {
        memcpy(&buffer_[current_], data, length);
      } else {
        memset(&buffer_[current_], 0, length);
      }
      // Pad with zeroes until buffer size is aligned.
      memset(&buffer_[current_ + length], 0, RTA_ALIGN(length) - length);
      current_ += RTA_ALIGN(length);
    }

    template <typename T>
    T* AppendAs(const T* data) {
      T* target = static_cast<T*>(static_cast<void*>(&buffer_[current_]));
      Append(data, sizeof(T));
      return target;
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
  nlattr* attr = request_.AppendAs<nlattr>(NULL);
  attr->nla_type = type;
  attr->nla_len = RTA_LENGTH(data_length);
  request_.Append(data, data_length);
  return attr;
}

NetlinkRequestImpl::NetlinkRequestImpl(
    int32_t command, int32_t flags)
    : header_(request_.AppendAs<nlmsghdr>(NULL)) {
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
  ifinfomsg* if_info = request_.AppendAs<ifinfomsg>(NULL);
  if_info->ifi_family = AF_UNSPEC;
  if_info->ifi_index = if_index;
  if_info->ifi_flags = operational ? IFF_UP : 0;
  if_info->ifi_change = IFF_UP;
}

void NetlinkRequestImpl::AddAddrInfo(int32_t if_index) {
  ifaddrmsg* ad_info = request_.AppendAs<ifaddrmsg>(NULL);
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

// NetlinkClient implementation.
// Talks to libnetlink to apply network changes.
class NetlinkClientImpl : public NetlinkClient {
 public:
  NetlinkClientImpl() = default;
  virtual ~NetlinkClientImpl() = default;

  virtual int32_t NameToIndex(const std::string& name);
  virtual bool Send(NetlinkRequest* message);

  // Initialize NetlinkClient instance.
  // Open netlink channel and initialize interface list.
  // Returns true, if initialization was successful.
  bool OpenNetlink();

 private:
  bool CheckResponse(uint32_t seq_no);

  SharedFD netlink_fd_;
  SharedFD network_fd_;
  int seq_no_;
};

int32_t NetlinkClientImpl::NameToIndex(const std::string& name) {
  // NOTE: do not replace this code with an IOCTL call.
  // On SELinux enabled Androids, RILD is not permitted to execute an IOCTL
  // and this call will fail.
  return if_nametoindex(name.c_str());
}

bool NetlinkClientImpl::CheckResponse(uint32_t seq_no) {
  uint32_t len;
  char buf[4096];
  struct iovec iov = { buf, sizeof(buf) };
  struct sockaddr_nl sa;
  struct msghdr msg = { &sa, sizeof(sa), &iov, 1, NULL, 0, 0 };
  struct nlmsghdr *nh;

  int result = netlink_fd_->RecvMsg(&msg, 0);
  if (result  < 0) {
    LOG(ERROR) << "Netlink error: " << strerror(errno);
    return false;
  }

  len = (uint32_t)result;
  LOG(INFO) << "Received netlink response (" << len << " bytes)";

  for (nh = (struct nlmsghdr*)buf; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
    if (nh->nlmsg_seq != seq_no) {
      // This really shouldn't happen. If we see this, it means somebody is
      // issuing netlink requests using the same socket as us, and ignoring
      // responses.
      LOG(WARNING) << "Sequence number mismatch: "
                   << nh->nlmsg_seq << " != " << seq_no;
      continue;
    }

    // This is the end of multi-part message.
    // It indicates there's nothing more netlink wants to tell us.
    // It also means we failed to find the response to our request.
    if (nh->nlmsg_type == NLMSG_DONE)
      break;

    // This is the 'nlmsgerr' package carrying response to our request.
    // It carries an 'error' value (errno) along with the netlink header info
    // that caused this error.
    if (nh->nlmsg_type == NLMSG_ERROR) {
      nlmsgerr* err = reinterpret_cast<nlmsgerr*>(nh + 1);
      if (err->error < 0) {
        LOG(ERROR) << "Failed to complete netlink request: "
                   << ": " << strerror(errno);
        return false;
      }
      return true;
    }
  }

  LOG(ERROR) << "No response from netlink.";
  return false;
}

bool NetlinkClientImpl::Send(NetlinkRequest* message) {
  message->SetSeqNo(seq_no_++);

  struct sockaddr_nl netlink_addr;
  struct iovec netlink_iov = {
    message->RequestData(),
    message->RequestLength()
  };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  memset(&netlink_addr, 0, sizeof(netlink_addr));

  netlink_addr.nl_family = AF_NETLINK;
  msg.msg_name = &netlink_addr;
  msg.msg_namelen = sizeof(netlink_addr);
  msg.msg_iov = &netlink_iov;
  msg.msg_iovlen = sizeof(netlink_iov) / sizeof(iovec);

  if (netlink_fd_->SendMsg(&msg, 0) < 0) {
    LOG(ERROR) << "Failed to send netlink message: "
               << ": " << strerror(errno);

    return false;
  }

  return CheckResponse(message->SeqNo());
}

bool NetlinkClientImpl::OpenNetlink() {
  netlink_fd_ = SharedFD::Socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  network_fd_ = SharedFD::Socket(AF_UNIX, SOCK_DGRAM, 0);
  seq_no_ = 0;
  return true;
}
}  // namespace

std::unique_ptr<NetlinkRequest> NetlinkRequest::New(
    NetlinkRequest::RequestType type) {
  int target_type = 0;
  int target_flags = 0;

  switch (type) {
    case RequestType::NewLink:
      target_type = RTM_NEWLINK;
      target_flags = NLM_F_CREATE | NLM_F_EXCL;
      break;

    case RequestType::SetLink:
      target_type = RTM_NEWLINK;
      break;

    case RequestType::AddAddress:
      target_type = RTM_NEWADDR;
      target_flags = NLM_F_CREATE | NLM_F_EXCL;
      break;

    case RequestType::DelAddress:
      target_type = RTM_DELADDR;
      break;
  }

  target_flags |= NLM_F_ACK | NLM_F_REQUEST;

  return std::unique_ptr<NetlinkRequest>(new NetlinkRequestImpl(
      target_type, target_flags));
}

NetlinkClient* NetlinkClient::New() {
  NetlinkClientImpl* client = new NetlinkClientImpl();

  if (!client->OpenNetlink()) {
    delete client;
    client = NULL;
  }
  return client;
}

}  // namespace avd

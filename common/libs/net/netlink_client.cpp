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
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/socket.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/glog/logging.h"

namespace avd {
namespace {
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

NetlinkClient* NetlinkClient::New() {
  NetlinkClientImpl* client = new NetlinkClientImpl();

  if (!client->OpenNetlink()) {
    delete client;
    client = NULL;
  }
  return client;
}

}  // namespace avd

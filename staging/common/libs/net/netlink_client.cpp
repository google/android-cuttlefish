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
#include "android-base/logging.h"

namespace cuttlefish {
namespace {
// NetlinkClient implementation.
// Talks to libnetlink to apply network changes.
class NetlinkClientImpl : public NetlinkClient {
 public:
  NetlinkClientImpl() = default;
  virtual ~NetlinkClientImpl() = default;

  virtual bool Send(const NetlinkRequest& message);

  // Initialize NetlinkClient instance.
  // Open netlink channel and initialize interface list.
  // Parameter |type| specifies which netlink target to address, eg.
  // NETLINK_ROUTE.
  // Returns true, if initialization was successful.
  bool OpenNetlink(int type);

 private:
  bool CheckResponse(uint32_t seq_no);

  SharedFD netlink_fd_;
  sockaddr_nl address_;
};

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

  len = static_cast<uint32_t>(result);
  LOG(INFO) << "Received netlink response (" << len << " bytes)";

  for (nh = reinterpret_cast<nlmsghdr*>(buf);
       NLMSG_OK(nh, len);
       nh = NLMSG_NEXT(nh, len)) {
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
                   << "Netlink error: " << err->error
                   << ", errno: " << strerror(errno);
        return false;
      }
      return true;
    }
  }

  LOG(ERROR) << "No response from netlink.";
  return false;
}

bool NetlinkClientImpl::Send(const NetlinkRequest& message) {
  struct sockaddr_nl netlink_addr;
  struct iovec netlink_iov = {
    message.RequestData(),
    message.RequestLength()
  };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  memset(&netlink_addr, 0, sizeof(netlink_addr));

  msg.msg_name = &address_;
  msg.msg_namelen = sizeof(address_);
  msg.msg_iov = &netlink_iov;
  msg.msg_iovlen = sizeof(netlink_iov) / sizeof(iovec);

  if (netlink_fd_->SendMsg(&msg, 0) < 0) {
    LOG(ERROR) << "Failed to send netlink message: "
               << strerror(errno);

    return false;
  }

  return CheckResponse(message.SeqNo());
}

bool NetlinkClientImpl::OpenNetlink(int type) {
  netlink_fd_ = SharedFD::Socket(AF_NETLINK, SOCK_RAW, type);
  if (!netlink_fd_->IsOpen()) return false;

  address_.nl_family = AF_NETLINK;
  address_.nl_groups = 0;

  netlink_fd_->Bind(reinterpret_cast<sockaddr*>(&address_), sizeof(address_));

  return true;
}

class NetlinkClientFactoryImpl : public NetlinkClientFactory {
 public:
  NetlinkClientFactoryImpl() = default;
  ~NetlinkClientFactoryImpl() override = default;

  std::unique_ptr<NetlinkClient> New(int type) override {
    auto client_raw = new NetlinkClientImpl();
    // Use RVO when possible.
    std::unique_ptr<NetlinkClient> client(client_raw);

    if (!client_raw->OpenNetlink(type)) {
      // Note: deletes client_raw.
      client.reset();
    }
    return client;
  }
};

}  // namespace

NetlinkClientFactory* NetlinkClientFactory::Default() {
  static NetlinkClientFactory &factory = *new NetlinkClientFactoryImpl();
  return &factory;
}

}  // namespace cuttlefish

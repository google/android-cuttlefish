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
#include "common/libs/wifi/router.h"

#include <cerrno>
#include <cstddef>
#include <map>
#include <memory>
#include <set>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

DEFINE_string(socket_name, "cvd-wifirouter",
              "Name of the unix-domain socket providing access for routing. "
              "Socket will be created in abstract namespace.");

namespace cvd {
namespace {
// Copied out of mac80211_hwsim.h header.
constexpr int HWSIM_CMD_REGISTER = 1;
constexpr int HWSIM_ATTR_ADDR_TRANSMITTER = 2;
constexpr int HWSIM_ATTR_MAX = 19;

// Name of the WIFI SIM Netlink Family.
constexpr char kWifiSimFamilyName[] = "MAC80211_HWSIM";
const int kMaxSupportedPacketSize = getpagesize();

class WifiRouter {
 public:
  using MacHash = uint16_t;
  using MacToClientsTable = std::multimap<MacHash, int>;
  using ClientsTable = std::set<int>;

  WifiRouter() : sock_(nullptr, nl_socket_free) {}
  ~WifiRouter() = default;

  void Init();
  void ServerLoop();

 private:
  MacHash GetMacHash(const void* macaddr);
  void CreateWifiRouterServerSocket();

  void RegisterForHWSimNotifications();
  void RouteWIFIPacket();

  void AcceptNewClient();
  bool HandleClientMessage(int client);
  void RemoveClient(int client);

  std::unique_ptr<nl_sock, void (*)(nl_sock*)> sock_;
  int server_fd_ = 0;
  int mac80211_family_ = 0;
  ClientsTable registered_clients_;
  MacToClientsTable registered_addresses_;
};

WifiRouter::MacHash WifiRouter::GetMacHash(const void* macaddr) {
  const uint8_t* t = reinterpret_cast<const uint8_t*>(macaddr);

  // This is guaranteed to be unique. Address here is assigned at creation time
  // and is (well) non-mutable. This is a unique ID of the MAC80211 HWSIM
  // interface.
  return t[3] << 8 | t[4];
}

void WifiRouter::Init() {
  CreateWifiRouterServerSocket();
  RegisterForHWSimNotifications();
}

// Enable asynchronous notifications from MAC80211_HWSIM.
// - `sock` is a valid netlink socket connected to NETLINK_GENERIC,
// - `family` is MAC80211_HWSIM genl family number.
//
// Upon failure, this function will terminate execution of the program.
void WifiRouter::RegisterForHWSimNotifications() {
  sock_.reset(nl_socket_alloc());

  auto res = nl_connect(sock_.get(), NETLINK_GENERIC);
  if (res < 0) {
    LOG(ERROR) << "Could not connect to netlink generic: " << nl_geterror(res);
    exit(1);
  }

  mac80211_family_ = genl_ctrl_resolve(sock_.get(), kWifiSimFamilyName);
  if (mac80211_family_ <= 0) {
    LOG(ERROR) << "Could not find MAC80211 HWSIM. Please make sure module "
               << "'mac80211_hwsim' is loaded on your system.";
    exit(1);
  }

  std::unique_ptr<nl_msg, void (*)(nl_msg*)> msg(
      nlmsg_alloc(), [](nl_msg* m) { nlmsg_free(m); });
  genlmsg_put(msg.get(), NL_AUTO_PID, NL_AUTO_SEQ, mac80211_family_, 0,
              NLM_F_REQUEST, HWSIM_CMD_REGISTER, 0);
  nl_send_auto(sock_.get(), msg.get());

  res = nl_wait_for_ack(sock_.get());
  if (res < 0) {
    LOG(ERROR) << "Could not register for notifications: " << nl_geterror(res);
    exit(1);
  }
}

// Create and configure WIFI Router server socket.
// This function is guaranteed to success. If at any point an error is detected,
// the function will terminate execution of the program.
void WifiRouter::CreateWifiRouterServerSocket() {
  server_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (server_fd_ < 0) {
    LOG(ERROR) << "Could not create unix socket: " << strerror(-errno);
    exit(1);
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  auto len = std::min(sizeof(addr.sun_path) - 2, FLAGS_socket_name.size());
  strncpy(&addr.sun_path[1], FLAGS_socket_name.c_str(), len);
  len += offsetof(sockaddr_un, sun_path) + 1;  // include heading \0 byte.
  auto res = bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), len);

  if (res < 0) {
    LOG(ERROR) << "Could not bind unix socket: " << strerror(-errno);
    exit(1);
  }

  listen(server_fd_, 4);
}

// Accept new WIFI Router client. When successful, client will be placed in
// clients table.
void WifiRouter::AcceptNewClient() {
  auto client = accept(server_fd_, nullptr, nullptr);
  if (client < 0) {
    LOG(ERROR) << "Could not accept client: " << strerror(-errno);
    return;
  }

  registered_clients_.insert(client);
  LOG(INFO) << "Client " << client << " added.";
}

// Disconnect and remove client from list of registered clients and recipients
// of WLAN traffic.
void WifiRouter::RemoveClient(int client) {
  close(client);
  registered_clients_.erase(client);

  for (auto iter = registered_addresses_.begin();
       iter != registered_addresses_.end();) {
    if (iter->second == client) {
      iter = registered_addresses_.erase(iter);
    } else {
      ++iter;
    }
  }
  LOG(INFO) << "Client " << client << " removed.";
}

// Read MAC80211HWSIM packet, find the originating MAC address and redirect it
// to proper sink.
void WifiRouter::RouteWIFIPacket() {
  sockaddr_nl tmp;
  uint8_t* buf;

  const auto len = nl_recv(sock_.get(), &tmp, &buf, nullptr);
  if (len < 0) {
    LOG(ERROR) << "Could not read from netlink: " << nl_geterror(len);
    return;
  }

  std::unique_ptr<nlmsghdr, void (*)(nlmsghdr*)> msg(
      reinterpret_cast<nlmsghdr*>(buf), [](nlmsghdr* m) { free(m); });

  // Discard messages that originate from anything else than MAC80211_HWSIM.
  if (msg->nlmsg_type != mac80211_family_) return;

  std::unique_ptr<nl_msg, void (*)(nl_msg*)> rep(
      nlmsg_alloc(), [](nl_msg* m) { nlmsg_free(m); });
  genlmsg_put(rep.get(), 0, 0, 0, 0, 0, WIFIROUTER_CMD_NOTIFY, 0);

  // Note, this is generic netlink message, and uses different parsing
  // technique.
  nlattr* attrs[HWSIM_ATTR_MAX + 1];
  if (genlmsg_parse(msg.get(), 0, attrs, HWSIM_ATTR_MAX, nullptr)) return;

  std::set<int> pending_removals;
  auto addr = attrs[HWSIM_ATTR_ADDR_TRANSMITTER];
  if (addr != nullptr) {
    nla_put_u32(rep.get(), WIFIROUTER_ATTR_HWSIM_ID, GetMacHash(nla_data(addr)));
    nla_put(rep.get(), WIFIROUTER_ATTR_PACKET, len, buf);
    auto hdr = nlmsg_hdr(rep.get());

    auto key = GetMacHash(nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]));
    LOG(INFO) << "Received netlink packet from " << std::hex << key;
    for (auto it = registered_addresses_.find(key);
         it != registered_addresses_.end() && it->first == key; ++it) {
      auto num_written = send(it->second, hdr, hdr->nlmsg_len, MSG_NOSIGNAL);
      if (num_written != static_cast<int64_t>(hdr->nlmsg_len)) {
        pending_removals.insert(it->second);
      }
    }

    for (auto client : pending_removals) RemoveClient(client);
  }
}

bool WifiRouter::HandleClientMessage(int client) {
  std::unique_ptr<nlmsghdr, void (*)(nlmsghdr*)> msg(
      reinterpret_cast<nlmsghdr*>(malloc(kMaxSupportedPacketSize)),
      [](nlmsghdr* h) { free(h); });
  int64_t size = recv(client, msg.get(), kMaxSupportedPacketSize, 0);

  // Invalid message or no data -> client invalid or disconnected.
  if (size == 0 || size != msg->nlmsg_len || size < sizeof(nlmsghdr)) {
    return false;
  }

  int result = -EINVAL;
  genlmsghdr* ghdr = reinterpret_cast<genlmsghdr*>(nlmsg_data(msg.get()));

  switch (ghdr->cmd) {
    case WIFIROUTER_CMD_REGISTER:
      // Register client to receive notifications for specified MAC address.
      nlattr* attrs[WIFIROUTER_ATTR_MAX];
      if (!nlmsg_parse(msg.get(), sizeof(genlmsghdr), attrs,
                       WIFIROUTER_ATTR_MAX - 1, nullptr)) {
        if (attrs[WIFIROUTER_ATTR_HWSIM_ID] != nullptr) {
          LOG(INFO) << "Registering new client to receive data for "
                    << nla_get_u32(attrs[WIFIROUTER_ATTR_HWSIM_ID]);
          registered_addresses_.emplace(
              nla_get_u32(attrs[WIFIROUTER_ATTR_HWSIM_ID]), client);
          // This is unfortunate, but it is a bug in mac80211_hwsim stack.
          // Apparently, the imperfect medium will not receive notifications for
          // newly created wifi interfaces. How about that...
          RegisterForHWSimNotifications();
          result = 0;
        }
      }
      break;

    default:
      break;
  }

  nlmsgerr err{.error = result};
  std::unique_ptr<nl_msg, void (*)(nl_msg*)> rsp(nlmsg_alloc(), nlmsg_free);
  nlmsg_put(rsp.get(), msg->nlmsg_pid, msg->nlmsg_seq, NLMSG_ERROR, 0, 0);
  nlmsg_append(rsp.get(), &err, sizeof(err), 0);
  auto hdr = nlmsg_hdr(rsp.get());
  if (send(client, hdr, hdr->nlmsg_len, MSG_NOSIGNAL) !=
      static_cast<int64_t>(hdr->nlmsg_len)) {
    return false;
  }
  return true;
}

// Process incoming requests from netlink, server or clients.
void WifiRouter::ServerLoop() {
  while (true) {
    auto max_fd = 0;
    fd_set reads{};

    auto fdset = [&max_fd, &reads](int fd) {
      FD_SET(fd, &reads);
      max_fd = std::max(max_fd, fd);
    };

    fdset(server_fd_);
    fdset(nl_socket_get_fd(sock_.get()));
    for (int client : registered_clients_) fdset(client);

    if (select(max_fd + 1, &reads, nullptr, nullptr, nullptr) <= 0) continue;

    if (FD_ISSET(server_fd_, &reads)) AcceptNewClient();
    if (FD_ISSET(nl_socket_get_fd(sock_.get()), &reads)) RouteWIFIPacket();

    // Process any client messages left. Drop any client that is no longer
    // talking with us.
    for (auto client = registered_clients_.begin();
         client != registered_clients_.end();) {
      auto cfd = *client++;
      // Is our client sending us data?
      if (FD_ISSET(cfd, &reads)) {
        if (!HandleClientMessage(cfd)) RemoveClient(cfd);
      }
    }
  }
}

}  // namespace
}  // namespace cvd

int main(int argc, char* argv[]) {
  using namespace cvd;
  google::ParseCommandLineFlags(&argc, &argv, true);
#if !defined(ANDROID)
  // We should check for legitimate google logging here.
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
#endif

  WifiRouter r;
  r.Init();
  r.ServerLoop();
}

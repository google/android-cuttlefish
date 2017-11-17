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
#include <cerrno>
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

namespace {
using MacHash = uint64_t;
using MacToClientsTable = std::multimap<MacHash, int>;
using ClientsTable = std::set<int>;

// Copied out of mac80211_hwsim.h header.
constexpr int HWSIM_CMD_REGISTER = 1;
constexpr int HWSIM_ATTR_ADDR_TRANSMITTER = 2;
constexpr int HWSIM_ATTR_MAX = 19;

// Name of the WIFI SIM Netlink Family.
constexpr char kWifiSimFamilyName[] = "MAC80211_HWSIM";
const int kMaxSupportedPacketSize = getpagesize();
constexpr uint16_t kWifiRouterType = ('W' << 8 | 'R');

enum {
  WIFIROUTER_ATTR_MAC,

  // Keep this last.
  WIFIROUTER_ATTR_MAX
};

// Get hash for mac address serialized to 6 bytes of data starting at specified
// location.
// We don't care about byte ordering as much as we do about having all bytes
// there. Byte order does not matter, we want to use it as a key in our map.
uint64_t GetMacHash(const void* macaddr) {
  auto typed = reinterpret_cast<const uint16_t*>(macaddr);
  return (1ull * typed[0] << 32) | (typed[1] << 16) | typed[2];
}

// Enable asynchronous notifications from MAC80211_HWSIM.
// - `sock` is a valid netlink socket connected to NETLINK_GENERIC,
// - `family` is MAC80211_HWSIM genl family number.
//
// Upon failure, this function will terminate execution of the program.
void RegisterForHWSimNotifications(nl_sock* sock, int family) {
  std::unique_ptr<nl_msg, void (*)(nl_msg*)> msg(
      nlmsg_alloc(), [](nl_msg* m) { nlmsg_free(m); });
  genlmsg_put(msg.get(), NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_REQUEST,
              HWSIM_CMD_REGISTER, 0);
  nl_send_auto(sock, msg.get());
  auto res = nl_wait_for_ack(sock);
  LOG_IF(FATAL, res < 0) << "Could not register for notifications: "
                         << nl_geterror(res);
}

// Create and configure WIFI Router server socket.
// This function is guaranteed to success. If at any point an error is detected,
// the function will terminate execution of the program.
int CreateWifiRouterServerSocket() {
  auto fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  LOG_IF(FATAL, fd <= 0) << "Could not create unix socket: " << strerror(-fd);

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path + 1, FLAGS_socket_name.c_str(),
          sizeof(addr.sun_path) - 2);

  auto res = bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  LOG_IF(FATAL, res < 0) << "Could not bind unix socket: " << strerror(-res);
  listen(fd, 4);
  return fd;
}

// Accept new WIFI Router client. When successful, client will be placed in
// clients table.
void AcceptNewClient(int server_fd, ClientsTable* clients) {
  auto client = accept(server_fd, nullptr, nullptr);
  LOG_IF(ERROR, client < 0) << "Could not accept client: " << strerror(errno);
  if (client > 0) clients->insert(client);
}

// Disconnect and remove client from list of registered clients and recipients
// of WLAN traffic.
void RemoveClient(int client, ClientsTable* clients,
                  MacToClientsTable* targets) {
  close(client);
  clients->erase(client);
  for (auto iter = targets->begin(); iter != targets->end();) {
    if (iter->second == client) {
      iter = targets->erase(iter);
    } else {
      ++iter;
    }
  }
}

// Read MAC80211HWSIM packet, find the originating MAC address and redirect it
// to proper sink.
void RouteWIFIPacket(nl_sock* nl, int simfamily, ClientsTable* clients,
                     MacToClientsTable* targets) {
  sockaddr_nl tmp;
  uint8_t* buf;

  const auto len = nl_recv(nl, &tmp, &buf, nullptr);
  if (len < 0) {
    LOG(ERROR) << "Could not read from netlink: " << nl_geterror(len);
    return;
  }

  std::unique_ptr<nlmsghdr, void (*)(nlmsghdr*)> msg(
      reinterpret_cast<nlmsghdr*>(buf), [](nlmsghdr* m) { free(m); });

  // Discard messages that originate from anything else than MAC80211_HWSIM.
  if (msg->nlmsg_type != simfamily) return;

  // Note, this is generic netlink message, and uses different parsing
  // technique.
  nlattr* attrs[HWSIM_ATTR_MAX + 1];
  if (genlmsg_parse(msg.get(), 0, attrs, HWSIM_ATTR_MAX, nullptr)) return;

  std::set<int> pending_removals;
  if (attrs[HWSIM_ATTR_ADDR_TRANSMITTER] != nullptr) {
    auto key = GetMacHash(nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]));
    VLOG(2) << "Received netlink packet from " << std::hex << key;
    for (auto it = targets->find(key); it != targets->end() && it->first == key;
         ++it) {
      auto num_written = write(it->second, buf, len);
      if (num_written != len) pending_removals.insert(it->second);
    }

    for (auto client : pending_removals) {
      RemoveClient(client, clients, targets);
    }
  }
}

void HandleClientMessage(int client, ClientsTable* clients,
                         MacToClientsTable* targets) {
  std::unique_ptr<nlmsghdr, void (*)(nlmsghdr*)> msg(
      reinterpret_cast<nlmsghdr*>(malloc(kMaxSupportedPacketSize)),
      [](nlmsghdr* h) { free(h); });
  auto size = read(client, msg.get(), kMaxSupportedPacketSize);

  // Invalid message or no data -> client invalid or disconnected.
  if (size == 0 || size != msg->nlmsg_len || size < sizeof(nlmsghdr)) {
    RemoveClient(client, clients, targets);
    return;
  }

  // Accept message, but ignore it.
  if (msg->nlmsg_type != kWifiRouterType) return;

  nlattr* attrs[WIFIROUTER_ATTR_MAX];
  if (!nlmsg_parse(msg.get(), 0, attrs, WIFIROUTER_ATTR_MAX - 1, nullptr)) {
    RemoveClient(client, clients, targets);
    return;
  }

  if (attrs[WIFIROUTER_ATTR_MAC] != nullptr) {
    targets->emplace(GetMacHash(nla_data(attrs[WIFIROUTER_ATTR_MAC])), client);
  }
}

// Process incoming requests from netlink, server or clients.
void ServerLoop(int server_fd, nl_sock* netlink_sock, int family) {
  ClientsTable clients;
  MacToClientsTable targets;
  int netlink_fd = nl_socket_get_fd(netlink_sock);

  while (true) {
    auto max_fd = server_fd;
    fd_set reads{};

    auto fdset = [&max_fd, &reads](int fd) {
      FD_SET(fd, &reads);
      max_fd = std::max(max_fd, fd);
    };

    fdset(server_fd);
    fdset(netlink_fd);
    for (int client : clients) fdset(client);

    if (select(max_fd + 1, &reads, nullptr, nullptr, nullptr) <= 0) continue;

    if (FD_ISSET(server_fd, &reads)) AcceptNewClient(server_fd, &clients);
    if (FD_ISSET(netlink_fd, &reads))
      RouteWIFIPacket(netlink_sock, family, &clients, &targets);
    for (int client : clients) {
      if (FD_ISSET(client, &reads)) {
        HandleClientMessage(client, &clients, &targets);
      }
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  std::unique_ptr<nl_sock, void (*)(nl_sock*)> sock(nl_socket_alloc(),
                                                    nl_socket_free);

  auto res = nl_connect(sock.get(), NETLINK_GENERIC);
  LOG_IF(FATAL, res < 0) << "Could not connect to netlink generic: "
                         << nl_geterror(res);

  auto mac80211_family = genl_ctrl_resolve(sock.get(), kWifiSimFamilyName);
  LOG_IF(FATAL, mac80211_family <= 0)
      << "Could not find MAC80211 HWSIM. Please make sure module "
      << "'mac80211_hwsim' is loaded on your system.";

  RegisterForHWSimNotifications(sock.get(), mac80211_family);
  auto server_fd = CreateWifiRouterServerSocket();
  ServerLoop(server_fd, sock.get(), mac80211_family);
}

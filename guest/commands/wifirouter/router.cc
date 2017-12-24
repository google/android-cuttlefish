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
#include <iomanip>
#include <map>
#include <memory>
#include <set>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <linux/netdevice.h>
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
DEFINE_bool(use_fixed_addresses, false,
            "Specify to use hard-coded WIFI addresses issued by MAC80211 HWSIM."
            " This is relevant for systems, where mac address update is not"
            " reflected in mac80211_hwsim module.");
DEFINE_bool(log_broadcast_frames, false, "Specify to log broadcast frames.");

namespace cvd {
// Copied out of mac80211_hwsim.h header.
constexpr int HWSIM_CMD_REGISTER = 1;
constexpr int HWSIM_CMD_FRAME = 2;
constexpr int HWSIM_CMD_TX_INFO_FRAME __unused = 3;

constexpr int HWSIM_TX_CTL_REQ_TX_STATUS __unused = 1;
constexpr int HWSIM_TX_CTL_NO_ACK __unused = 2;
constexpr int HWSIM_TX_STAT_ACK __unused = 4;

constexpr int HWSIM_ATTR_ADDR_RECEIVER = 1;
constexpr int HWSIM_ATTR_ADDR_TRANSMITTER = 2;
constexpr int HWSIM_ATTR_FRAME = 3;
constexpr int HWSIM_ATTR_FLAGS __unused = 4;
constexpr int HWSIM_ATTR_RX_RATE = 5;
constexpr int HWSIM_ATTR_SIGNAL = 6;
constexpr int HWSIM_ATTR_TX_INFO __unused = 7;
constexpr int HWSIM_ATTR_COOKIE __unused = 8;
constexpr int HWSIM_ATTR_MAX = 19;

// Name of the WIFI SIM Netlink Family.
constexpr char kWifiSimFamilyName[] = "MAC80211_HWSIM";
const int kMaxSupportedPacketSize = getpagesize();

constexpr int kDefaultSignalLevel = -24;

using MACAddress = uint8_t[6];

struct IEEE80211Hdr {
  uint16_t frame_control;
  uint16_t duration_id;
  MACAddress destination;
  MACAddress source;
  MACAddress bssid;
  uint16_t seq;

  bool IsBroadcast() const;
} __attribute__((packed));

std::ostream& operator<<(std::ostream& out, const MACAddress& addr) {
  out << std::hex
      << std::setfill('0') << std::setw(2) << int(addr[0]) << ':'
      << std::setfill('0') << std::setw(2) << int(addr[1]) << ':'
      << std::setfill('0') << std::setw(2) << int(addr[2]) << ':'
      << std::setfill('0') << std::setw(2) << int(addr[3]) << ':'
      << std::setfill('0') << std::setw(2) << int(addr[4]) << ':'
      << std::setfill('0') << std::setw(2) << int(addr[5]) << std::dec;

  return out;
}

std::ostream& operator<<(std::ostream& out, const IEEE80211Hdr& frm) {
  out << "IEEE80211Hdr{ Type=" << std::hex << std::setw(4) << std::setfill('0')
      << frm.frame_control << std::dec
      << " From=" << frm.source
      << " To=" << frm.destination << " Via=" << frm.bssid << " }";
  return out;
}

bool IEEE80211Hdr::IsBroadcast() const {
  return (destination[0] & destination[1] & destination[2] & destination[3] &
      destination[4] & destination[5]) == 0xff;
}

class WifiRouter {
 public:
  using RadioID = int32_t;
  using Radio = struct {
    RadioID id;
    uint8_t mac[ETH_ALEN];
  };
  using RadioToClientsTable = std::multimap<RadioID, int>;
  using ClientToRadiosTable = std::multimap<int, Radio>;
  using MacAddrToRadioIDTable = std::map<uint64_t, RadioID>;
  const RadioID RadioID_Invalid = -1;

  WifiRouter() : sock_(nullptr, nl_socket_free) {}
  ~WifiRouter() = default;

  void Init();
  void ServerLoop();

 private:
  void AddRadioID(int client, RadioID radio_id, const void* macaddr);
  RadioID GetRadioID(const void* macaddr);
  void CreateWifiRouterServerSocket();

  void RegisterForHWSimNotifications();
  void RouteWIFIPacket();

  void AcceptNewClient();
  bool HandleClientMessage(int client);
  void RemoveClient(int client);

  std::unique_ptr<nl_sock, void (*)(nl_sock*)> sock_;
  int server_fd_ = 0;
  int mac80211_family_ = 0;
  ClientToRadiosTable registered_clients_;
  RadioToClientsTable registered_addresses_;
  MacAddrToRadioIDTable known_addresses_;
};

void WifiRouter::AddRadioID(int client, RadioID radio_id, const void* macaddr) {
  const uint8_t* addr_bytes = reinterpret_cast<const uint8_t*>(macaddr);
  uint64_t mac;
  Radio r{radio_id, {}};

  mac = (addr_bytes[0] << 24) | (addr_bytes[1] << 16) | (addr_bytes[2] >> 8) |
      addr_bytes[3];
  mac <<= 16;
  mac |= (addr_bytes[4] << 8) | addr_bytes[5];

  known_addresses_[mac] = radio_id;
  // Add two MAC addresses registered internally by MAC80211_HWSIM.
  mac = 0x020000000000ull;
  mac |= (radio_id << 8);
  known_addresses_[mac] = radio_id;

  mac |= 0x400000000000ull;
  known_addresses_[mac] = radio_id;

  if (FLAGS_use_fixed_addresses) {
    r.mac[0] = mac >> 40;
    r.mac[1] = mac >> 32;
    r.mac[2] = mac >> 24;
    r.mac[3] = mac >> 16;
    r.mac[4] = mac >> 8;
    r.mac[5] = mac;
  } else {
    memcpy(r.mac, macaddr, ETH_ALEN);
  }
  registered_addresses_.emplace(radio_id, client);
  registered_clients_.emplace(client, r);
}

WifiRouter::RadioID WifiRouter::GetRadioID(const void* macaddr) {
  const uint8_t* addr_bytes = reinterpret_cast<const uint8_t*>(macaddr);
  uint64_t mac;

  mac = (addr_bytes[0] << 24) | (addr_bytes[1] << 16) | (addr_bytes[2] >> 8) |
      addr_bytes[3];
  mac <<= 16;
  mac |= (addr_bytes[4] << 8) | addr_bytes[5];

  auto iter = known_addresses_.find(mac);
  if (iter == known_addresses_.end()) return RadioID_Invalid;
  return iter->second;
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

  // Disable sequence number checks. Occasional "Message sequence number
  // mismatch" errors were observed, despite netlink allocating sequence numbers
  // itself.
  nl_socket_disable_seq_check(sock_.get());

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

  registered_clients_.insert({client, {RadioID_Invalid, {}}});
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

  genlmsghdr* gmsg = reinterpret_cast<genlmsghdr*>(nlmsg_data(msg.get()));
  if (gmsg->cmd != HWSIM_CMD_FRAME) {
    LOG(INFO) << "Discarding non-FRAME message.";
    return;
  }

  std::unique_ptr<nl_msg, void (*)(nl_msg*)> rep(
      nlmsg_alloc(), [](nl_msg* m) { nlmsg_free(m); });
  genlmsg_put(rep.get(), 0, 0, 0, 0, 0, WIFIROUTER_CMD_NOTIFY, 0);

  // Note, this is generic netlink message, and uses different parsing
  // technique.
  nlattr* attrs[HWSIM_ATTR_MAX + 1];
  if (genlmsg_parse(msg.get(), 0, attrs, HWSIM_ATTR_MAX, nullptr)) return;

  auto ieee80211hdr = reinterpret_cast<IEEE80211Hdr*>(nla_data(attrs[HWSIM_ATTR_FRAME]));
  if (!ieee80211hdr->IsBroadcast() || FLAGS_log_broadcast_frames) {
    LOG(INFO) << "SND " << *ieee80211hdr;
  }

  std::set<int> pending_removals;
  auto addr = attrs[HWSIM_ATTR_ADDR_TRANSMITTER];
  if (addr != nullptr) {
    nla_put_u32(rep.get(), WIFIROUTER_ATTR_HWSIM_ID,
                GetRadioID(nla_data(addr)));
    nla_put(rep.get(), WIFIROUTER_ATTR_PACKET, len, buf);
    auto hdr = nlmsg_hdr(rep.get());

    auto key = GetRadioID(nla_data(attrs[HWSIM_ATTR_ADDR_TRANSMITTER]));
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
  ssize_t size = recv(client, msg.get(), kMaxSupportedPacketSize, 0);

  // Invalid message or no data -> client invalid or disconnected.
  if (size == 0 || size != static_cast<ssize_t>(msg->nlmsg_len) ||
      size < static_cast<ssize_t>(sizeof(nlmsghdr))) {
    return false;
  }

  int result = -EINVAL;
  genlmsghdr* ghdr = reinterpret_cast<genlmsghdr*>(nlmsg_data(msg.get()));

  nlattr* attrs[WIFIROUTER_ATTR_MAX];
  if (nlmsg_parse(msg.get(), sizeof(genlmsghdr), attrs, WIFIROUTER_ATTR_MAX - 1,
                  nullptr))
    return false;

  switch (ghdr->cmd) {
    case WIFIROUTER_CMD_REGISTER:
      if (attrs[WIFIROUTER_ATTR_HWSIM_ID] != nullptr) {
        int simid = nla_get_u32(attrs[WIFIROUTER_ATTR_HWSIM_ID]);
        uint8_t* simaddr = reinterpret_cast<uint8_t*>(
            nla_data(attrs[WIFIROUTER_ATTR_HWSIM_ADDR]));

        AddRadioID(client, simid, simaddr);
        // This is unfortunate, but it is a bug in mac80211_hwsim stack.
        // Apparently, the imperfect medium will not receive notifications for
        // newly created wifi interfaces. How about that...
        RegisterForHWSimNotifications();
        result = 0;
      }
      break;

    case WIFIROUTER_CMD_SEND:
      if (attrs[WIFIROUTER_ATTR_PACKET] != nullptr) {
        std::unique_ptr<nl_msg, void (*)(nl_msg*)> frame(
            nlmsg_convert(reinterpret_cast<nlmsghdr*>(
                nla_data(attrs[WIFIROUTER_ATTR_PACKET]))),
            nlmsg_free);

        // Netlink is not smart enough to re-alloc.
        nlmsg_expand(frame.get(), nlmsg_get_max_size(frame.get()) + 64);

        auto hdr = nlmsg_hdr(frame.get());
        hdr->nlmsg_type = mac80211_family_;
        hdr->nlmsg_flags = NLM_F_REQUEST;

        auto pktdata = nlmsg_find_attr(nlmsg_hdr(frame.get()),
                                     sizeof(genlmsghdr),
                                     HWSIM_ATTR_FRAME);
        auto ieee80211hdr = reinterpret_cast<IEEE80211Hdr*>(nla_data(pktdata));
        if (!ieee80211hdr->IsBroadcast() || FLAGS_log_broadcast_frames) {
          LOG(INFO) << "RCV " << *ieee80211hdr;
        }

        auto receiver =
            nla_reserve(frame.get(), HWSIM_ATTR_ADDR_RECEIVER, ETH_ALEN);
        if (nla_put_u32(frame.get(), HWSIM_ATTR_RX_RATE, 1) ||
            nla_put_u32(frame.get(), HWSIM_ATTR_SIGNAL, kDefaultSignalLevel) ||
            !receiver) {
          LOG(ERROR) << "Could not add netlink attribute: buffer too short.";
        } else {
          uint8_t* macaddr = reinterpret_cast<uint8_t*>(nla_data(receiver));
          for (auto iter = registered_clients_.find(client);
               iter->first == client; ++iter) {
            if (iter->second.id == RadioID_Invalid) continue;
            memcpy(macaddr, iter->second.mac, ETH_ALEN);
            hdr->nlmsg_seq = NL_AUTO_SEQ;
            hdr->nlmsg_pid = NL_AUTO_PID;
            nl_send_auto(sock_.get(), frame.get());
            auto res = nl_wait_for_ack(sock_.get());
            if (res) {
              LOG(INFO) << "Packet send from " << client << " to "
                        << iter->second.id << " result: " << nl_geterror(-res);
            }
          }
        }
        result = 0;
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
    for (const auto& client : registered_clients_) fdset(client.first);

    if (select(max_fd + 1, &reads, nullptr, nullptr, nullptr) <= 0) continue;

    if (FD_ISSET(server_fd_, &reads)) AcceptNewClient();
    if (FD_ISSET(nl_socket_get_fd(sock_.get()), &reads)) RouteWIFIPacket();

    std::set<int> rogue_clients;
    // Process any client messages left. Drop any client that is no longer
    // talking with us.
    for (auto cfd : registered_clients_) {
      // Is our client sending us data?
      if (FD_ISSET(cfd.first, &reads)) {
        if (!HandleClientMessage(cfd.first)) rogue_clients.insert(cfd.first);
        // Note: we iterate over multimap.
        FD_CLR(cfd.first, &reads);
      }
    }

    for (auto client : rogue_clients) RemoveClient(client);
  }
}

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

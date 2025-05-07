/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <platform/api/udp_socket.h>

#include <sstream>
#include <string>

#include <android-base/logging.h>
#include <discovery/mdns/public/mdns_constants.h>

#include "adb_unique_fd.h"
#include "adb_utils.h"
#include "fdevent/fdevent.h"
#include "sysdeps.h"

//#define DEBUG_UDP

constexpr bool IsPowerOf2(uint32_t x) {
    return (x > 0) && ((x & (x - 1)) == 0);
}

static_assert(IsPowerOf2(alignof(adb_cmsghdr)), "std::align requires power-of-2 alignment");

// For windows, winuser.h defines a SendMessage macro that causes compilation issues.
#ifdef _WIN32
#ifdef SendMessage
#undef SendMessage
#endif
#endif
namespace openscreen {

namespace {

#ifdef _WIN32
using IPv4NetworkInterfaceIndex = decltype(ip_mreq().imr_interface.s_addr);
#else
using IPv4NetworkInterfaceIndex = decltype(ip_mreqn().imr_ifindex);
#endif
using IPv6NetworkInterfaceIndex = decltype(ipv6_mreq().ipv6mr_interface);

// Examine |posix_errno| to determine whether the specific cause of a failure
// was transient or hard, and return the appropriate error response.
Error ChooseError(decltype(errno) posix_errno, Error::Code hard_error_code) {
    if (posix_errno == EAGAIN || posix_errno == EWOULDBLOCK || posix_errno == ENOBUFS) {
        return Error(Error::Code::kAgain, strerror(errno));
    }
    return Error(hard_error_code, strerror(errno));
}

IPAddress GetIPAddressFromSockAddr(const sockaddr_in& sa) {
    static_assert(IPAddress::kV4Size == sizeof(sa.sin_addr.s_addr), "IPv4 address size mismatch.");
    return IPAddress(IPAddress::Version::kV4,
                     reinterpret_cast<const uint8_t*>(&sa.sin_addr.s_addr));
}

IPAddress GetIPAddressFromPktInfo(const in_pktinfo& pktinfo) {
    static_assert(IPAddress::kV4Size == sizeof(pktinfo.ipi_addr), "IPv4 address size mismatch.");
    return IPAddress(IPAddress::Version::kV4, reinterpret_cast<const uint8_t*>(&pktinfo.ipi_addr));
}

uint16_t GetPortFromFromSockAddr(const sockaddr_in& sa) {
    return ntohs(sa.sin_port);
}

IPAddress GetIPAddressFromSockAddr(const sockaddr_in6& sa) {
    return IPAddress(IPAddress::Version::kV6, sa.sin6_addr.s6_addr);
}

IPAddress GetIPAddressFromPktInfo(const in6_pktinfo& pktinfo) {
    return IPAddress(IPAddress::Version::kV6, pktinfo.ipi6_addr.s6_addr);
}

uint16_t GetPortFromFromSockAddr(const sockaddr_in6& sa) {
    return ntohs(sa.sin6_port);
}

template <class PktInfoType>
bool IsPacketInfo(adb_cmsghdr* cmh);

template <>
bool IsPacketInfo<in_pktinfo>(adb_cmsghdr* cmh) {
    return cmh->cmsg_level == IPPROTO_IP && cmh->cmsg_type == IP_PKTINFO;
}

template <>
bool IsPacketInfo<in6_pktinfo>(adb_cmsghdr* cmh) {
    return cmh->cmsg_level == IPPROTO_IPV6 && cmh->cmsg_type == IPV6_PKTINFO;
}

template <class SockAddrType, class PktInfoType>
Error ReceiveMessageInternal(borrowed_fd fd, UdpPacket* packet) {
    SockAddrType sa;
    adb_iovec iov;
    iov.iov_len = packet->size();
    iov.iov_base = packet->data();
    alignas(alignof(adb_cmsghdr)) uint8_t control_buffer[1024];
    adb_msghdr msg;
    msg.msg_name = &sa;
    msg.msg_namelen = sizeof(sa);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);
    msg.msg_flags = 0;

    ssize_t bytes_received = adb_recvmsg(fd, &msg, 0);
    if (bytes_received == -1) {
        return ChooseError(errno, Error::Code::kSocketReadFailure);
    }

    CHECK_EQ(static_cast<size_t>(bytes_received), packet->size());

    IPEndpoint source_endpoint = {.address = GetIPAddressFromSockAddr(sa),
                                  .port = GetPortFromFromSockAddr(sa)};
    packet->set_source(std::move(source_endpoint));

    // For multicast sockets, the packet's original destination address may be
    // the host address (since we called bind()) but it may also be a
    // multicast address.  This may be relevant for handling multicast data;
    // specifically, mDNSResponder requires this information to work properly.

    socklen_t sa_len = sizeof(sa);
    if (((msg.msg_flags & MSG_CTRUNC) != 0) ||
        (adb_getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &sa_len) == -1)) {
        return Error::Code::kNone;
    }
    for (adb_cmsghdr* cmh = adb_CMSG_FIRSTHDR(&msg); cmh; cmh = adb_CMSG_NXTHDR(&msg, cmh)) {
        if (IsPacketInfo<PktInfoType>(cmh)) {
            PktInfoType* pktinfo = reinterpret_cast<PktInfoType*>(adb_CMSG_DATA(cmh));
            IPEndpoint destination_endpoint = {.address = GetIPAddressFromPktInfo(*pktinfo),
                                               .port = GetPortFromFromSockAddr(sa)};
            packet->set_destination(destination_endpoint);
            break;
        }
    }
    return Error::Code::kNone;
}
// An open UDP socket for sending/receiving datagrams to/from either specific
// endpoints or over IP multicast.
//
// Usage: The socket is created and opened by calling the Create() method. This
// returns a unique pointer that auto-closes/destroys the socket when it goes
// out-of-scope.
class AdbUdpSocket : public UdpSocket {
  public:
    explicit AdbUdpSocket(UdpSocket::Client* client, const IPEndpoint& local_endpoint, unique_fd fd)
        : client_(client), local_endpoint_(local_endpoint), fd_(std::move(fd)) {
        CHECK(client_);
        CHECK(local_endpoint_.address.IsV4() || local_endpoint_.address.IsV6());
        fde_ = fdevent_create(fd_.get(), OnFdeventResult, this);
        if (!fde_) {
            LOG(FATAL) << "Unable to create fdevent";
            return;
        }
        fdevent_set(fde_, FDE_READ);
        VLOG(MDNS) << " fd=" << fd_.get();
    }

    ~AdbUdpSocket() override {
        if (fde_) {
            fdevent_destroy(fde_);
        }
    }

    // Returns true if |socket| belongs to the IPv4/IPv6 address family.
    bool IsIPv4() const override { return local_endpoint_.address.IsV4(); }
    bool IsIPv6() const override { return local_endpoint_.address.IsV6(); }

    // Returns the current local endpoint's address and port. Initially, this will
    // be the same as the value that was passed into Create(). However, it can
    // later change after certain operations, such as Bind(), are executed.
    IPEndpoint GetLocalEndpoint() const override {
        if (local_endpoint_.port == 0) {
            // Note: If the getsockname() call fails, just assume that's because the
            // socket isn't bound yet. In this case, leave the original value in-place.
            switch (local_endpoint_.address.version()) {
                case UdpSocket::Version::kV4: {
                    struct sockaddr_in address;
                    socklen_t address_len = sizeof(address);
                    if (adb_getsockname(fd_, reinterpret_cast<struct sockaddr*>(&address),
                                        &address_len) == 0) {
                        CHECK_EQ(address.sin_family, AF_INET);
                        local_endpoint_.address =
                                IPAddress(IPAddress::Version::kV4,
                                          reinterpret_cast<uint8_t*>(&address.sin_addr.s_addr));
                        local_endpoint_.port = ntohs(address.sin_port);
                    }
                    break;
                }

                case UdpSocket::Version::kV6: {
                    struct sockaddr_in6 address;
                    socklen_t address_len = sizeof(address);
                    if (adb_getsockname(fd_, reinterpret_cast<struct sockaddr*>(&address),
                                        &address_len) == 0) {
                        CHECK_EQ(address.sin6_family, AF_INET6);
                        local_endpoint_.address =
                                IPAddress(IPAddress::Version::kV6,
                                          reinterpret_cast<uint8_t*>(&address.sin6_addr));
                        local_endpoint_.port = ntohs(address.sin6_port);
                    }
                    break;
                }
            }
        }

        return local_endpoint_;
    }

    // Binds to the address specified in the constructor. If the local endpoint's
    // address is zero, the operating system will bind to all interfaces. If the
    // local endpoint's port is zero, the operating system will automatically find
    // a free local port and bind to it. Future calls to GetLocalEndpoint() will
    // reflect the resolved port.
    //
    // TODO: openscreen does some chromium compat thing where it calls Bind() before
    // SetMulticastOutboundInterface(), because chromium alreadys sets IP_MULTICAST_IF internally
    // before calling Bind(). So we currently wait for the SetMulticastOutboundInterface() call from
    // the osp-discovery code before actually binding. Note that this means AdbUdpSocket is not for
    // the general use-case of udp sockets.
    void Bind() override {
        if (mdns_ifindex_) {
            // TODO: move MdnsBind() code back into here once osp-discovery calls Bind() after
            // SetMulticastOutboundInterface().
            LOG(FATAL) << "osp-discovery called Bind() after SetMulticastOutboundInterface()!";
        } else {
            // mdns impl will only call SetMulticastOutboundInterface and JoinMulticastGroup after
            // bind is successful.
            client_->OnBound(this);
        }
    }

    static void SetIPV4MulticastProperties(std::optional<IPAddress> local_ipv4,
                                           std::optional<IPAddress> multiaddr_ipv4,
                                           ip_mreq* result) {
        CHECK(result);
        static_assert(sizeof(result->imr_multiaddr) == 4u, "IPv4 address requires exactly 4 bytes");
        static_assert(sizeof(result->imr_interface) == 4u, "IPv4 address requires exactly 4 bytes");

        *result = {};
        if (local_ipv4) {
            local_ipv4->CopyToV4(reinterpret_cast<uint8_t*>(&result->imr_interface.s_addr));
        }

        if (multiaddr_ipv4) {
            multiaddr_ipv4->CopyToV4(reinterpret_cast<uint8_t*>(&result->imr_multiaddr));
        }
    }

    // Sets the device to use for outgoing multicast packets on the socket.
    void SetMulticastOutboundInterface(NetworkInterfaceIndex ifindex) override {
        if (!fd_.ok()) {
            OnError(Error::Code::kSocketClosedFailure);
            return;
        }

        // TODO: remove once osp-discovery calls Bind() after SetMulticastOutboundInterface().
        *mdns_ifindex_ = ifindex;

        VLOG(MDNS) << "SetMulticastOutboundInterface for index=" << ifindex;
        switch (local_endpoint_.address.version()) {
            case UdpSocket::Version::kV4: {
                struct ip_mreq multicast_properties = {};
                SetIPV4MulticastProperties(local_endpoint_.address, std::nullopt,
                                           &multicast_properties);
#ifdef DEBUG_UDP
                struct in_addr default_addr;
                unsigned int buf_size = sizeof(default_addr);
                if (getsockopt(fd_.get(), IPPROTO_IP, IP_MULTICAST_IF, &default_addr, &buf_size) !=
                    -1) {
                    const auto default_ip =
                            IPAddress(IPAddress::Version::kV4,
                                      reinterpret_cast<const uint8_t*>(&default_addr.s_addr));
                    VLOG(MDNS) << "BEFORE IP_MULTICAST_IF: default multicast addr=" << default_ip;
                }
#endif  // DEBUG_UDP
                if (adb_setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &multicast_properties,
                                   sizeof(multicast_properties)) == -1) {
                    OnError(Error::Code::kSocketOptionSettingFailure);
                    PLOG(ERROR) << "adb_setsockopt() failed";
                    return;
                }
#ifdef DEBUG_UDP
#ifndef _WIN32
                buf_size = sizeof(default_addr);
                if (getsockopt(fd_.get(), IPPROTO_IP, IP_MULTICAST_IF, &default_addr, &buf_size) !=
                    -1) {
                    const auto default_ip =
                            IPAddress(IPAddress::Version::kV4,
                                      reinterpret_cast<const uint8_t*>(&default_addr.s_addr));
                    LOG(INFO) << "AFTER IP_MULTICAST_IF: default multicast addr=" << default_ip;
                }
#endif  // !_WIN32
#endif  // DEBUG_UDP
                break;
            }
            case UdpSocket::Version::kV6: {
                const auto index = static_cast<IPv6NetworkInterfaceIndex>(ifindex);
                if (adb_setsockopt(fd_, IPPROTO_IPV6, IPV6_MULTICAST_IF, &index, sizeof(index)) ==
                    -1) {
                    OnError(Error::Code::kSocketOptionSettingFailure);
                    PLOG(ERROR) << "adb_setsockopt() failed";
                    return;
                }
                break;
            }
        }

        // TODO: remove once osp-discovery calls Bind() after SetMulticastOutboundInterface().
        MdnsBind(ifindex);
    }

    // Joins to the multicast group at the given address, using the specified
    // interface.
    void JoinMulticastGroup(const IPAddress& address, NetworkInterfaceIndex ifindex) override {
        if (!fd_.ok()) {
            OnError(Error::Code::kSocketClosedFailure);
            return;
        }

        switch (local_endpoint_.address.version()) {
            case UdpSocket::Version::kV4: {
                // Passed as data to setsockopt().  1 means return IP_PKTINFO control data
                // in recvmsg() calls.
                const int enable_pktinfo = 1;
                if (adb_setsockopt(fd_, IPPROTO_IP, IP_PKTINFO, &enable_pktinfo,
                                   sizeof(enable_pktinfo)) == -1) {
                    OnError(Error::Code::kSocketOptionSettingFailure);
                    LOG(ERROR) << "adb_setsockopt failed";
                    return;
                }
                struct ip_mreq multicast_properties;
                SetIPV4MulticastProperties(local_endpoint_.address, address, &multicast_properties);
                if (adb_setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multicast_properties,
                                   sizeof(multicast_properties)) == -1) {
                    OnError(Error::Code::kSocketOptionSettingFailure);
                    LOG(ERROR) << "adb_setsockopt failed";
                    return;
                }
                return;
            }

            case UdpSocket::Version::kV6: {
                // Passed as data to setsockopt().  1 means return IPV6_PKTINFO control
                // data in recvmsg() calls.
                const int enable_pktinfo = 1;
#ifdef _WIN32
                if (adb_setsockopt(fd_, IPPROTO_IPV6, IPV6_PKTINFO, &enable_pktinfo,
                                   sizeof(enable_pktinfo)) == -1) {
#else
                if (adb_setsockopt(fd_, IPPROTO_IPV6, IPV6_RECVPKTINFO, &enable_pktinfo,
                                   sizeof(enable_pktinfo)) == -1) {
#endif
                    OnError(Error::Code::kSocketOptionSettingFailure);
                    LOG(ERROR) << "adb_setsockopt failed";
                    return;
                }
                struct ipv6_mreq multicast_properties = {
                        .ipv6mr_multiaddr = {},
                        .ipv6mr_interface = static_cast<IPv6NetworkInterfaceIndex>(ifindex),
                };
                static_assert(sizeof(multicast_properties.ipv6mr_multiaddr) == 16u,
                              "IPv6 address requires exactly 16 bytes");
                address.CopyToV6(
                        reinterpret_cast<uint8_t*>(&multicast_properties.ipv6mr_multiaddr));
                // Portability note: All platforms support IPV6_JOIN_GROUP, which is
                // synonymous with IPV6_ADD_MEMBERSHIP.
                if (adb_setsockopt(fd_, IPPROTO_IPV6, IPV6_JOIN_GROUP, &multicast_properties,
                                   sizeof(multicast_properties)) == -1) {
                    OnError(Error::Code::kSocketOptionSettingFailure);
                    LOG(ERROR) << "adb_setsockopt failed";
                    return;
                }
                return;
            }
        }
    }

    // Sends a message. If the message is not sent, Client::OnSendError() will be
    // called to indicate this. Error::Code::kAgain indicates the operation would
    // block, which can be expected during normal operation.
    virtual void SendMessage(const void* data, size_t length, const IPEndpoint& dest) override {
        if (!fd_.ok()) {
            client_->OnSendError(this, Error::Code::kSocketClosedFailure);
            return;
        }

        adb_iovec iov;
        iov.iov_len = length;
        iov.iov_base = const_cast<void*>(data);

        adb_msghdr msg;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        ssize_t num_bytes_sent = -2;
        switch (local_endpoint_.address.version()) {
            case UdpSocket::Version::kV4: {
                struct sockaddr_in sa = {};
                sa.sin_family = AF_INET;
                sa.sin_port = htons(dest.port);
                dest.address.CopyToV4(reinterpret_cast<uint8_t*>(&sa.sin_addr.s_addr));
                msg.msg_name = &sa;
                msg.msg_namelen = sizeof(sa);
                num_bytes_sent = adb_sendmsg(fd_, &msg, 0);
                break;
            }

            case UdpSocket::Version::kV6: {
                struct sockaddr_in6 sa = {};
                sa.sin6_family = AF_INET6;
                sa.sin6_flowinfo = 0;
                sa.sin6_scope_id = 0;
                sa.sin6_port = htons(dest.port);
                dest.address.CopyToV6(reinterpret_cast<uint8_t*>(&sa.sin6_addr.s6_addr));
                msg.msg_name = &sa;
                msg.msg_namelen = sizeof(sa);
                num_bytes_sent = adb_sendmsg(fd_, &msg, 0);
                break;
            }
        }

        // Some VPN result in "short send" where less than the full datagram is reported sent. We
        // shield ourselves from these and hypothetical "long send" and plain errors by reporting
        // any unexpected return value.
        if (num_bytes_sent != (ssize_t)length) {
            LOG(WARNING) << "Error: sendmsg datagram size=" << length << " sent=" << num_bytes_sent;
            client_->OnSendError(this, ChooseError(errno, Error::Code::kSocketSendFailure));
            return;
        }
    }

    // Sets the DSCP value to use for all messages sent from this socket.
    void SetDscp(DscpMode state) override {
#ifdef _WIN32
        // TODO: this method doesn't seem to be used anywhere in the openscreen code, so
        // ignoring implementation for now.
        // Windows 10 seems to ignore setsockopt IP_TOS, so need to use Win APIs instead.
        LOG(FATAL) << "NOT IMPLEMENTED";
#else   // !_WIN32
        if (!fd_.ok()) {
            OnError(Error::Code::kSocketClosedFailure);
            return;
        }

        constexpr auto kSettingLevel = IPPROTO_IP;
        uint8_t code_array[1] = {static_cast<uint8_t>(state)};
        auto code = adb_setsockopt(fd_, kSettingLevel, IP_TOS, code_array, sizeof(uint8_t));

        if (code == EBADF || code == ENOTSOCK || code == EFAULT) {
            OnError(Error::Code::kSocketOptionSettingFailure);
            LOG(WARNING) << "BAD SOCKET PROVIDED. CODE: " << code;
            return;
        } else if (code == EINVAL) {
            OnError(Error::Code::kSocketOptionSettingFailure);
            LOG(WARNING) << "INVALID DSCP INFO PROVIDED";
            return;
        } else if (code == ENOPROTOOPT) {
            OnError(Error::Code::kSocketOptionSettingFailure);
            LOG(WARNING) << "INVALID DSCP SETTING LEVEL PROVIDED: " << kSettingLevel;
            return;
        }
#endif  // _WIN32
    }

  private:
    // TODO: Move back into public Bind() call once osp-discovery code calls Bind() after
    // SetMulticastOutboundInterface().
    void MdnsBind(NetworkInterfaceIndex ifindex) {
        if (!fd_.ok()) {
            OnError(Error::Code::kSocketClosedFailure);
            LOG(ERROR) << "Bind() failed. Socket is closed.";
            return;
        }

        // This is effectively a boolean passed to setsockopt() to allow a future
        // bind() on the same socket to succeed, even if the address is already in
        // use. This is pretty much universally the desired behavior.
        const int reuse = 1;
        if (adb_setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
            OnError(Error::Code::kSocketOptionSettingFailure);
            LOG(WARNING) << "Failed to set SO_REUSEADDR";
            return;
        }

#if defined(__APPLE__)
        // On Mac, SO_REUSEADDR is not enough to allow a bind() on a reusable multicast socket.
        // We need to also set the option SO_REUSEPORT.
        if (adb_setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
            OnError(Error::Code::kSocketOptionSettingFailure);
            LOG(WARNING) << "Failed to set SO_REUSEPORT";
            return;
        }
#endif

        switch (local_endpoint_.address.version()) {
            case UdpSocket::Version::kV4: {
                struct sockaddr_in address = {};
                address.sin_family = AF_INET;
                address.sin_port = htons(local_endpoint_.port);
                // MUST bind to ADDR_ANY to send and receive multicast messages.
                address.sin_addr.s_addr = INADDR_ANY;
                if (adb_bind(fd_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) ==
                    -1) {
                    OnError(Error::Code::kSocketBindFailure);
                    PLOG(ERROR) << "adb_bind failed";
                    return;
                }

                // Get the resolved address/port
                sockaddr_in sa;
                socklen_t sa_len = sizeof(sa);
                if (adb_getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &sa_len) != -1) {
                    local_endpoint_.address = GetIPAddressFromSockAddr(sa);
                    local_endpoint_.port = GetPortFromFromSockAddr(sa);
                    VLOG(MDNS) << "bind endpoint=" << local_endpoint_;
                }
                return;
            }
            case UdpSocket::Version::kV6: {
                struct sockaddr_in6 address = {};
                address.sin6_family = AF_INET6;
                address.sin6_flowinfo = 0;
                address.sin6_port = htons(local_endpoint_.port);
                // MUST bind to ADDR_ANY and scope_id unset to send and receive multicast messages.
                address.sin6_addr = in6addr_any;
                address.sin6_scope_id = 0;
                if (adb_bind(fd_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) ==
                    -1) {
                    OnError(Error::Code::kSocketBindFailure);
                    PLOG(ERROR) << "adb_bind failed";
                    return;
                }

                // Get the resolved address/port
                sockaddr_in6 sa;
                socklen_t sa_len = sizeof(sa);
                if (adb_getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &sa_len) != -1) {
                    local_endpoint_.address = GetIPAddressFromSockAddr(sa);
                    local_endpoint_.port = GetPortFromFromSockAddr(sa);
                    VLOG(MDNS) << "bind endpoint=" << local_endpoint_
                               << " scope_id=" << sa.sin6_scope_id;
                }
                return;
            }
            default:
                LOG(FATAL) << "Invalid domain";
                break;
        }
    }

    // Called by fdevent handler when data is available.
    void ReceiveMessage() {
        if (!fd_.ok()) {
            client_->OnRead(this, Error::Code::kSocketClosedFailure);
            return;
        }

        const auto bytes_available = network_peek(fd_);
        if (!bytes_available.has_value()) {
            client_->OnRead(this, ChooseError(errno, Error::Code::kSocketReadFailure));
            return;
        }

        UdpPacket packet(*bytes_available);
        packet.set_socket(this);
        Error result = Error::Code::kUnknownError;
        switch (local_endpoint_.address.version()) {
            case UdpSocket::Version::kV4: {
                result = ReceiveMessageInternal<sockaddr_in, in_pktinfo>(fd_, &packet);
                break;
            }
            case UdpSocket::Version::kV6: {
                result = ReceiveMessageInternal<sockaddr_in6, in6_pktinfo>(fd_, &packet);
                break;
            }
            default: {
                LOG(FATAL) << "Invalid domain";
                break;
            }
        }

        client_->OnRead(this, result.ok() ? ErrorOr<UdpPacket>(std::move(packet))
                                          : ErrorOr<UdpPacket>(std::move(result)));
    }

    void OnError(Error::Code error_code) {
        // Close the socket unless the error code represents a transient condition.
        if (error_code != Error::Code::kNone && error_code != Error::Code::kAgain) {
            if (fde_) {
                fdevent_destroy(fde_);
                fde_ = nullptr;
            }
        }

        std::stringstream stream;
        stream << "endpoint: " << local_endpoint_;
        client_->OnError(this, Error(error_code, stream.str()));
    }

    static void OnFdeventResult(int fd, unsigned ev, void* opaque) {
        AdbUdpSocket* s = reinterpret_cast<AdbUdpSocket*>(opaque);
        if (ev & FDE_READ) {
            s->ReceiveMessage();
        }
    }

    Client* const client_;
    mutable IPEndpoint local_endpoint_;
    unique_fd fd_;
    fdevent* fde_ = nullptr;
    std::optional<NetworkInterfaceIndex> mdns_ifindex_;
};

}  // namespace

// Implementation of openscreen's platform APIs for udp_socket.h
// static
ErrorOr<std::unique_ptr<UdpSocket>> UdpSocket::Create(TaskRunner* task_runner,
                                                      UdpSocket::Client* client,
                                                      const IPEndpoint& local_endpoint) {
    // |task_runner| is not used in adb's udp implementation because everything is going through the
    // fdevent thread when we register the fd.
    std::string err;
    std::stringstream ip_addr_ss;
    ip_addr_ss << local_endpoint.address;

    int domain;
    switch (local_endpoint.address.version()) {
        case Version::kV4:
            domain = AF_INET;
            break;
        case Version::kV6:
            domain = AF_INET6;
            break;
        default:
            LOG(FATAL) << "Invalid domain";
            return Error::Code::kInitializationFailure;
    }

    unique_fd fd(adb_socket(domain, SOCK_DGRAM, 0));
    if (!fd.ok()) {
        PLOG(ERROR) << "Failed to create udp socket";
        return Error::Code::kInitializationFailure;
    }

    if (!set_file_block_mode(fd, false)) {
        PLOG(ERROR) << "Failed to set non-block mode on fd";
        return Error::Code::kInitializationFailure;
    }

    VLOG(MDNS) << "UDP socket created for " << local_endpoint;
    std::unique_ptr<UdpSocket> udp_socket(new AdbUdpSocket(client, local_endpoint, std::move(fd)));
    return udp_socket;
}

}  // namespace openscreen

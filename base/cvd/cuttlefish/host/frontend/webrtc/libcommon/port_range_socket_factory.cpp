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

#include "host/frontend/webrtc/libcommon/port_range_socket_factory.h"

#include <android-base/logging.h>

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

std::pair<uint16_t, uint16_t> IntersectPortRanges(
    std::pair<uint16_t, uint16_t> own_range, uint16_t min_port,
    uint16_t max_port) {
  if (own_range.first == own_range.second && own_range.first == 0) {
    // No range configured
    return {min_port, max_port};
  }
  if (min_port == max_port && max_port == 0) {
    // No range requested, use configured
    return own_range;
  }
  uint16_t own_min_port = own_range.first;
  uint16_t own_max_port = own_range.second;

  if (min_port > own_max_port || max_port < own_min_port) {
    // Ranges don't intersect
    LOG(WARNING) << "Port ranges don't intersect: requested=[" << min_port
                 << "," << max_port << "], configured=[" << own_min_port << ","
                 << own_max_port << "]";
  }
  return {std::max(min_port, own_min_port), std::min(max_port, own_max_port)};
}

}  // namespace

PortRangeSocketFactory::PortRangeSocketFactory(
    rtc::SocketFactory* socket_factory, std::pair<uint16_t, uint16_t> udp_port_range,
    std::pair<uint16_t, uint16_t> tcp_port_range)
    : rtc::BasicPacketSocketFactory(socket_factory),
      udp_port_range_(udp_port_range),
      tcp_port_range_(tcp_port_range) {}

rtc::AsyncPacketSocket* PortRangeSocketFactory::CreateUdpSocket(
    const rtc::SocketAddress& local_address, uint16_t min_port,
    uint16_t max_port) {
  auto port_range = IntersectPortRanges(udp_port_range_, min_port, max_port);
  if (port_range.second < port_range.first) {
    // Own range doesn't intersect with requested range
    return nullptr;
  }
  return rtc::BasicPacketSocketFactory::CreateUdpSocket(
      local_address, port_range.first, port_range.second);
}

rtc::AsyncListenSocket* PortRangeSocketFactory::CreateServerTcpSocket(
    const rtc::SocketAddress& local_address, uint16_t min_port,
    uint16_t max_port, int opts) {
  auto port_range = IntersectPortRanges(tcp_port_range_, min_port, max_port);
  if (port_range.second < port_range.first) {
    // Own range doesn't intersect with requested range
    return nullptr;
  }

  return rtc::BasicPacketSocketFactory::CreateServerTcpSocket(
      local_address, port_range.first, port_range.second, opts);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish

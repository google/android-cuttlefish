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

#pragma once

#include <cinttypes>
#include <utility>

// This is not part of the webrtc api and therefore subject to change
#include <p2p/base/basic_packet_socket_factory.h>

namespace cuttlefish {
namespace webrtc_streaming {

// rtc::BasicPacketSocketFactory is not part of the webrtc api so only functions
// from its upper class should be overridden here.
class PortRangeSocketFactory : public rtc::BasicPacketSocketFactory {
 public:
  PortRangeSocketFactory(rtc::SocketFactory* socket_factory,
                         std::pair<uint16_t, uint16_t> udp_port_range,
                         std::pair<uint16_t, uint16_t> tcp_port_range);

  rtc::AsyncPacketSocket* CreateUdpSocket(
      const rtc::SocketAddress& local_address, uint16_t min_port,
      uint16_t max_port) override;

  rtc::AsyncListenSocket* CreateServerTcpSocket(
      const rtc::SocketAddress& local_address, uint16_t min_port,
      uint16_t max_port, int opts) override;

 private:
  std::pair<uint16_t, uint16_t> udp_port_range_;
  std::pair<uint16_t, uint16_t> tcp_port_range_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish

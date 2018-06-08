/*
 * Copyright (C) 2018 The Android Open Source Project
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
#include <iomanip>
#include <sstream>
#include <string>
#include <memory>
#include <glog/logging.h>

#include <unistd.h>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/adb_connection_maintainer/adb_connection_maintainer.h"

namespace {

std::string MakeMessage(const std::string& user_message) {
  static constexpr char kPrefix[] = "host:";
  static constexpr std::size_t kPrefixLen = sizeof kPrefix - 1;
  std::ostringstream ss;
  ss << std::setfill('0') << std::setw(4) << std::hex
     << (kPrefixLen + user_message.size()) << kPrefix << user_message;
  return ss.str();
}

std::string MakeIPAndPort(int port) {
  static constexpr char kLocalHostPrefix[] = "127.0.0.1:";
  return kLocalHostPrefix + std::to_string(port);
}

std::string MakeConnectMessage(int port) {
  static constexpr char kConnectPrefix[] = "connect:";
  return MakeMessage(kConnectPrefix + MakeIPAndPort(port));
}

// returns true if successfully sent the whole message
bool SendAll(cvd::SharedFD sock, const std::string& msg) {
  ssize_t total_written{};
  while (total_written < static_cast<ssize_t>(msg.size())) {
    if (!sock->IsOpen()) {
      return false;
    }
    auto just_written = sock->Send(msg.c_str() + total_written,
                                   msg.size() - total_written, MSG_NOSIGNAL);
    if (just_written <= 0) {
      return false;
    }
    total_written += just_written;
  }
  return true;
}

std::string RecvAll(cvd::SharedFD sock, const size_t count) {
  size_t total_read{};
  std::unique_ptr<char[]> data(new char[count]);
  while (total_read < count) {
    auto just_read = sock->Read(data.get() + total_read, count - total_read);
    if (just_read <= 0) {
      return {};
    }
    total_read += just_read;
  }
  return {data.get(), count};
}

// Response will either be OKAY or FAIL
constexpr char kAdbOkayStatusResponse[] = "OKAY";
constexpr std::size_t kAdbStatusResponseLength =
    sizeof kAdbOkayStatusResponse - 1;
// adb sends the length of what is to follow as a 4 characters string of hex
// digits
constexpr std::size_t kAdbMessageLengthLength = 4;

constexpr int kAdbDaemonPort = 5037;

bool AdbConnect(cvd::SharedFD sock, int port) {
  if (!SendAll(sock, MakeConnectMessage(port))) {
    return false;
  }
  return RecvAll(sock, kAdbStatusResponseLength) == kAdbOkayStatusResponse;
}

// assumes the OKAY/FAIL status has already been read
std::string RecvAdbResponse(cvd::SharedFD sock) {
  auto length_as_hex_str = RecvAll(sock, kAdbMessageLengthLength);
  auto length = std::stoi(length_as_hex_str, nullptr, 16);
  return RecvAll(sock, length);
}

void EstablishConnection(int port) {
  while (true) {
    LOG(INFO) << "Attempting to connect to device on port " << port;
    auto sock = cvd::SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
    if (sock->IsOpen() && AdbConnect(sock, port)) {
      LOG(INFO) << "connection attempted to device on port " << port;
      break;
    }
    sleep(2);
  }
}

void WaitForAdbDisconnection(int port) {
  LOG(INFO) << "Watching for disconnect on port " << port;
  while (true) {
    auto sock = cvd::SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
    if (!SendAll(sock, MakeMessage("devices"))) {
      break;
    }
    if (RecvAll(sock, 4) != kAdbOkayStatusResponse) {
      break;
    }
    auto devices_str = RecvAdbResponse(sock);
    if (devices_str.find(MakeIPAndPort(port)) == std::string::npos) {
      break;
    }
    sleep(2);
  }
}

}  // namespace

[[noreturn]] void cvd::EstablishAndMaintainConnection(int port) {
  while (true) {
    EstablishConnection(port);
    WaitForAdbDisconnection(port);
  }
}

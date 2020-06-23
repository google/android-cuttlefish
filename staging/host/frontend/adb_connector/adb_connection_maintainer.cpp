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

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <memory>
#include <vector>
#include <android-base/logging.h>

#include <unistd.h>

#include "common/libs/fs/shared_fd.h"
#include "host/frontend/adb_connector/adb_connection_maintainer.h"

namespace {

std::string MakeMessage(const std::string& user_message) {
  std::ostringstream ss;
  ss << std::setfill('0') << std::setw(4) << std::hex << user_message.size()
     << user_message;
  return ss.str();
}

std::string MakeShellUptimeMessage() {
  return MakeMessage("shell,raw:cut -d. -f1 /proc/uptime");
}

std::string MakeTransportMessage(const std::string& address) {
  return MakeMessage("host:transport:" + address);
}

std::string MakeConnectMessage(const std::string& address) {
  return MakeMessage("host:connect:" + address);
}

std::string MakeDisconnectMessage(const std::string& address) {
  return MakeMessage("host:connect:" + address);
}

// returns true if successfully sent the whole message
bool SendAll(cuttlefish::SharedFD sock, const std::string& msg) {
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

std::string RecvAll(cuttlefish::SharedFD sock, const size_t count) {
  size_t total_read{};
  std::unique_ptr<char[]> data(new char[count]);
  while (total_read < count) {
    auto just_read = sock->Read(data.get() + total_read, count - total_read);
    if (just_read <= 0) {
      LOG(WARNING) << "adb daemon socket closed early";
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

bool AdbSendMessage(cuttlefish::SharedFD sock, const std::string& message) {
  if (!sock->IsOpen()) {
    return false;
  }
  if (!SendAll(sock, message)) {
    LOG(WARNING) << "failed to send all bytes to adb daemon";
    return false;
  }
  return RecvAll(sock, kAdbStatusResponseLength) == kAdbOkayStatusResponse;
}

bool AdbSendMessage(const std::string& message) {
  auto sock = cuttlefish::SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
  return AdbSendMessage(sock, message);
}

bool AdbConnect(const std::string& address) {
  return AdbSendMessage(MakeConnectMessage(address));
}

bool AdbDisconnect(const std::string& address) {
  return AdbSendMessage(MakeDisconnectMessage(address));
}

bool IsInteger(const std::string& str) {
  return !str.empty() && std::all_of(str.begin(), str.end(),
                                     [](char c) { return std::isdigit(c); });
}

// assumes the OKAY/FAIL status has already been read
std::string RecvAdbResponse(cuttlefish::SharedFD sock) {
  auto length_as_hex_str = RecvAll(sock, kAdbMessageLengthLength);
  if (!IsInteger(length_as_hex_str)) {
    return {};
  }
  auto length = std::stoi(length_as_hex_str, nullptr, 16);
  return RecvAll(sock, length);
}

// Returns a negative value if uptime result couldn't be read for
// any reason.
int RecvUptimeResult(cuttlefish::SharedFD sock) {
  std::vector<char> uptime_vec{};
  std::vector<char> just_read(16);
  do {
    auto count = sock->Read(just_read.data(), just_read.size());
    if (count < 0) {
      LOG(WARNING) << "couldn't receive adb shell output";
      return -1;
    }
    just_read.resize(count);
    uptime_vec.insert(uptime_vec.end(), just_read.begin(), just_read.end());
  } while (!just_read.empty());

  if (uptime_vec.empty()) {
    LOG(WARNING) << "empty adb shell result";
    return -1;
  }

  uptime_vec.pop_back();

  auto uptime_str = std::string{uptime_vec.data(), uptime_vec.size()};
  if (!IsInteger(uptime_str)) {
    LOG(WARNING) << "non-numeric: uptime result: " << uptime_str;
    return -1;
  }

  return std::stoi(uptime_str);
}

// There needs to be a gap between the adb commands, the daemon isn't able to
// handle the avalanche of requests we would be sending without a sleep. Five
// seconds is much larger than seems necessary so we should be more than okay.
static constexpr int kAdbCommandGapTime = 5;

void EstablishConnection(const std::string& address) {
  LOG(DEBUG) << "Attempting to connect to device with address " << address;
  while (!AdbConnect(address)) {
    sleep(kAdbCommandGapTime);
  }
  LOG(DEBUG) << "adb connect message for " << address << " successfully sent";
  sleep(kAdbCommandGapTime);
}

void WaitForAdbDisconnection(const std::string& address) {
  // adb daemon doesn't seem to handle quick, successive messages well. The
  // sleeps stabilize the communication.
  LOG(DEBUG) << "Watching for disconnect on " << address;
  while (true) {
    auto sock = cuttlefish::SharedFD::SocketLocalClient(kAdbDaemonPort, SOCK_STREAM);
    if (!AdbSendMessage(sock, MakeTransportMessage(address))) {
      LOG(WARNING) << "transport message failed, response body: "
                   << RecvAdbResponse(sock);
      break;
    }
    if (!AdbSendMessage(sock, MakeShellUptimeMessage())) {
      LOG(WARNING) << "adb shell uptime message failed";
      break;
    }

    auto uptime = RecvUptimeResult(sock);
    if (uptime < 0) {
      LOG(WARNING) << "couldn't read uptime result";
      break;
    }
    LOG(VERBOSE) << "device on " << address << " uptime " << uptime;
    sleep(kAdbCommandGapTime);
  }
  LOG(DEBUG) << "Sending adb disconnect";
  AdbDisconnect(address);
  sleep(kAdbCommandGapTime);
}

}  // namespace

[[noreturn]] void cuttlefish::EstablishAndMaintainConnection(std::string address) {
  while (true) {
    EstablishConnection(address);
    WaitForAdbDisconnection(address);
  }
}

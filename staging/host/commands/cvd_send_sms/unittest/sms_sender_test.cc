//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/cvd_send_sms/sms_sender.h"

#include <sstream>

#include <android-base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {
namespace {

class SmsSenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &client_fd_,
                               &fake_server_fd_))
        << strerror(errno);
    CHECK(client_fd_->IsOpen());
    CHECK(fake_server_fd_->IsOpen());
  }

  void AssertCommandIsSent(std::string expected_command) {
    std::stringstream ss;
    std::vector<char> buffer(4096);
    ssize_t bytes_read;
    do {
      bytes_read = fake_server_fd_->Read(buffer.data(), buffer.size());
      CHECK(bytes_read >= 0) << strerror(errno);
      ss << std::string(buffer.data(), bytes_read);
    } while (buffer[bytes_read - 1] != '\r');
    EXPECT_THAT(ss.str(), testing::Eq(expected_command));
  }

  SharedFD client_fd_;
  SharedFD fake_server_fd_;
};

TEST_F(SmsSenderTest, InvalidContentFails) {
  SmsSender sender(client_fd_);

  bool result = sender.Send("", "+16501234567");

  EXPECT_FALSE(result);
}

TEST_F(SmsSenderTest, ValidContentSucceeds) {
  SmsSender sender(client_fd_);

  bool result = sender.Send("hellohello", "+16501234567");

  EXPECT_TRUE(result);
  AssertCommandIsSent(
      "REM0AT+REMOTESMS=0001000b916105214365f700000ae8329bfd4697d9ec37\r");
}

TEST_F(SmsSenderTest, NonDefaultModemIdValueSucceeds) {
  SmsSender sender(client_fd_);

  bool result = sender.Send("hellohello", "+16501234567", 1);

  EXPECT_TRUE(result);
  AssertCommandIsSent(
      "REM1AT+REMOTESMS=0001000b916105214365f700000ae8329bfd4697d9ec37\r");
}

}  // namespace
}  // namespace cuttlefish

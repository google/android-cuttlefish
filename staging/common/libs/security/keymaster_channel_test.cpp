/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/security/keymaster_channel_sharedfd.h"
#include "gtest/gtest.h"

namespace cuttlefish {

TEST(KeymasterChannel, SendAndReceiveRequest) {
  SharedFD read_fd;
  SharedFD write_fd;
  ASSERT_TRUE(SharedFD::Pipe(&read_fd, &write_fd)) << "Failed to create pipe";

  SharedFdKeymasterChannel channel{read_fd, write_fd};

  char buffer[] = {1, 2, 3, 4, 5, 6};
  keymaster::Buffer request(buffer, sizeof(buffer));

  ASSERT_TRUE(channel.SendRequest(keymaster::GET_VERSION, request))
      << "Failed to send request";
  auto response = channel.ReceiveMessage();
  EXPECT_EQ(response->cmd, keymaster::GET_VERSION) << "Command mismatch";
  EXPECT_FALSE(response->is_response) << "Request/response mismatch";

  keymaster::Buffer read;
  const uint8_t* read_data = response->payload;
  EXPECT_TRUE(read.Deserialize(&read_data, read_data + response->payload_size))
      << "Failed to deserialize request";
  ASSERT_EQ(request.end() - request.begin(), read.end() - read.begin());
  ASSERT_EQ(request.buffer_size(), read.buffer_size());
  ASSERT_TRUE(std::equal(request.begin(), request.end(), read.begin()));
}

TEST(KeymasterChannel, SendAndReceiveResponse) {
  SharedFD read_fd;
  SharedFD write_fd;
  ASSERT_TRUE(SharedFD::Pipe(&read_fd, &write_fd)) << "Failed to create pipe";

  SharedFdKeymasterChannel channel{read_fd, write_fd};

  char buffer[] = {1, 2, 3, 4, 5, 6};
  keymaster::Buffer request(buffer, sizeof(buffer));

  ASSERT_TRUE(channel.SendResponse(keymaster::GET_VERSION, request))
      << "Failed to send request";
  auto response = channel.ReceiveMessage();
  EXPECT_EQ(response->cmd, keymaster::GET_VERSION) << "Command mismatch";
  EXPECT_TRUE(response->is_response) << "Request/response mismatch";

  keymaster::Buffer read;
  const uint8_t* read_data = response->payload;
  EXPECT_TRUE(read.Deserialize(&read_data, read_data + response->payload_size))
      << "Failed to deserialize request";
  ASSERT_EQ(request.end() - request.begin(), read.end() - read.begin());
  ASSERT_EQ(request.buffer_size(), read.buffer_size());
  ASSERT_TRUE(std::equal(request.begin(), request.end(), read.begin()));
}

}  // namespace cuttlefish

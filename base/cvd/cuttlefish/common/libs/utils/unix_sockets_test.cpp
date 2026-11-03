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

#include "cuttlefish/common/libs/utils/unix_sockets.h"

#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {

Result<Fd> CreateMemFDWithData(const std::string& data) {
  Fd memfd = CF_EXPECT(Fd::MemfdCreate(""));
  CF_EXPECT(WriteExact(memfd, data));
  CF_EXPECT(memfd.SeekSet(0));
  return memfd;
}

TEST(UnixSocketMessage, ExtractFileDescriptors) {
  const SharedFD memfd1 = CF_ASSERT(CreateMemFDWithData("abc"));
  const SharedFD memfd2 = CF_ASSERT(CreateMemFDWithData("def"));

  UnixSocketMessage message;
  message.control.emplace_back(
      CF_ASSERT(ControlMessage::FromFileDescriptors({memfd1})));
  message.control.emplace_back(
      CF_ASSERT(ControlMessage::FromFileDescriptors({memfd2})));

  EXPECT_TRUE(message.HasFileDescriptors());
  std::vector<SharedFD> fds = CF_ASSERT(message.FileDescriptors());
  EXPECT_THAT(ReadToString(*fds[0]), IsOkAndValue("abc"));
  EXPECT_THAT(ReadToString(*fds[1]), IsOkAndValue("def"));
}

Result<std::pair<UnixMessageSocket, UnixMessageSocket>>
UnixMessageSocketPair() {
  auto [sock1, sock2] = CF_EXPECT(Fd::SocketPair(AF_UNIX, SOCK_SEQPACKET, 0));
  return {{UnixMessageSocket(std::move(sock1)),
           UnixMessageSocket(std::move(sock2))}};
}

TEST(UnixMessageSocket, SendPlainMessage) {
  auto [writer, reader] = CF_ASSERT(UnixMessageSocketPair());
  const UnixSocketMessage message_in = {{1, 2, 3}, {}};
  EXPECT_THAT(writer.WriteMessage(message_in), IsOk());

  const UnixSocketMessage message_out = CF_ASSERT(reader.ReadMessage());
  EXPECT_EQ(message_in.data, message_out.data);
  EXPECT_EQ(0, message_out.control.size());
}

TEST(UnixMessageSocket, SendFileDescriptor) {
  auto [writer, reader] = CF_ASSERT(UnixMessageSocketPair());

  UnixSocketMessage message_in = {{4, 5, 6}, {}};
  message_in.control.emplace_back(CF_ASSERT(ControlMessage::FromFileDescriptors(
      {CF_ASSERT(CreateMemFDWithData("abc"))})));
  EXPECT_THAT(writer.WriteMessage(message_in), IsOk());

  const UnixSocketMessage message_out = CF_ASSERT(reader.ReadMessage());
  EXPECT_EQ(message_in.data, message_out.data);

  ASSERT_EQ(1, message_out.control.size());
  std::vector<SharedFD> fds_out =
      CF_ASSERT(message_out.control[0].AsSharedFDs());
  ASSERT_EQ(1, fds_out.size());
  EXPECT_THAT(ReadToString(*fds_out[0]), IsOkAndValue("abc"));
}

TEST(UnixMessageSocket, SendTwoFileDescriptors) {
  const SharedFD memfd1 = CF_ASSERT(CreateMemFDWithData("abc"));
  const SharedFD memfd2 = CF_ASSERT(CreateMemFDWithData("def"));

  auto [writer, reader] = CF_ASSERT(UnixMessageSocketPair());
  UnixSocketMessage message_in = {{7, 8, 9}, {}};
  message_in.control.emplace_back(
      CF_ASSERT(ControlMessage::FromFileDescriptors({memfd1, memfd2})));
  EXPECT_THAT(writer.WriteMessage(message_in), IsOk());

  const UnixSocketMessage message_out = CF_ASSERT(reader.ReadMessage());
  EXPECT_EQ(message_in.data, message_out.data);

  ASSERT_EQ(1, message_out.control.size());
  std::vector<SharedFD> fds_out =
      CF_ASSERT(message_out.control[0].AsSharedFDs());
  ASSERT_EQ(2, fds_out.size());

  EXPECT_THAT(ReadToString(*fds_out[0]), IsOkAndValue("abc"));
  EXPECT_THAT(ReadToString(*fds_out[1]), IsOkAndValue("def"));
}

TEST(UnixMessageSocket, SendCredentials) {
  auto [writer, reader] = CF_ASSERT(UnixMessageSocketPair());
  EXPECT_THAT(writer.EnableCredentials(true), IsOk());
  EXPECT_THAT(reader.EnableCredentials(true), IsOk());

  const ucred credentials_in = {
      .pid = getpid(),
      .uid = getuid(),
      .gid = getgid(),
  };
  UnixSocketMessage message_in = {{1, 5, 9}, {}};
  message_in.control.emplace_back(
      ControlMessage::FromCredentials(credentials_in));
  EXPECT_THAT(writer.WriteMessage(message_in), IsOk());

  const UnixSocketMessage message_out = CF_ASSERT(reader.ReadMessage());
  EXPECT_EQ(message_in.data, message_out.data);

  ASSERT_EQ(1, message_out.control.size());
  const ucred credentials_out =
      CF_ASSERT(message_out.control[0].AsCredentials());
  EXPECT_EQ(credentials_in.pid, credentials_out.pid);
  EXPECT_EQ(credentials_in.uid, credentials_out.uid);
  EXPECT_EQ(credentials_in.gid, credentials_out.gid);
}

TEST(UnixMessageSocket, BadCredentialsBlocked) {
  auto [writer, reader] = CF_ASSERT(UnixMessageSocketPair());
  EXPECT_THAT(writer.EnableCredentials(true), IsOk());
  EXPECT_THAT(reader.EnableCredentials(true), IsOk());

  // This assumes the test is running without root privileges
  const ucred credentials_in = {
      .pid = getpid() + 1,
      .uid = getuid() + 1,
      .gid = getgid() + 1,
  };

  UnixSocketMessage message_in = {{2, 4, 6}, {}};
  message_in.control.emplace_back(
      ControlMessage::FromCredentials(credentials_in));
  EXPECT_THAT(writer.WriteMessage(message_in), IsError());
}

TEST(UnixMessageSocket, AutoCredentials) {
  auto [writer, reader] = CF_ASSERT(UnixMessageSocketPair());
  EXPECT_THAT(writer.EnableCredentials(true), IsOk());
  EXPECT_THAT(reader.EnableCredentials(true), IsOk());

  const UnixSocketMessage message_in = {{3, 6, 9}, {}};
  EXPECT_THAT(writer.WriteMessage(message_in), IsOk());

  const UnixSocketMessage message_out = CF_ASSERT(reader.ReadMessage());
  EXPECT_EQ(message_in.data, message_out.data);

  ASSERT_EQ(1, message_out.control.size());
  const ucred credentials_out =
      CF_ASSERT(message_out.control[0].AsCredentials());
  EXPECT_EQ(getpid(), credentials_out.pid);
  EXPECT_EQ(getuid(), credentials_out.uid);
  EXPECT_EQ(getgid(), credentials_out.gid);
}

}  // namespace cuttlefish

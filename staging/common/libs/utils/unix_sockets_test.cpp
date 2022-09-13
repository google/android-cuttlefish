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

#include "common/libs/utils/unix_sockets.h"

#include <android-base/logging.h>
#include <android-base/result.h>
#include <gtest/gtest.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

SharedFD CreateMemFDWithData(const std::string& data) {
  auto memfd = SharedFD::MemfdCreate("");
  CHECK(WriteAll(memfd, data) == data.size()) << memfd->StrError();
  CHECK(memfd->LSeek(0, SEEK_SET) == 0);
  return memfd;
}

std::string ReadAllFDData(SharedFD fd) {
  std::string data;
  CHECK(ReadAll(fd, &data) > 0) << fd->StrError();
  return data;
}

TEST(UnixSocketMessage, ExtractFileDescriptors) {
  auto memfd1 = CreateMemFDWithData("abc");
  auto memfd2 = CreateMemFDWithData("def");

  UnixSocketMessage message;
  auto control1 = ControlMessage::FromFileDescriptors({memfd1});
  ASSERT_TRUE(control1.ok()) << control1.error().Trace();
  message.control.emplace_back(std::move(*control1));
  auto control2 = ControlMessage::FromFileDescriptors({memfd2});
  ASSERT_TRUE(control2.ok()) << control2.error().Trace();
  message.control.emplace_back(std::move(*control2));

  ASSERT_TRUE(message.HasFileDescriptors());
  auto fds = message.FileDescriptors();
  ASSERT_TRUE(fds.ok());
  ASSERT_EQ("abc", ReadAllFDData((*fds)[0]));
  ASSERT_EQ("def", ReadAllFDData((*fds)[1]));
}

std::pair<UnixMessageSocket, UnixMessageSocket> UnixMessageSocketPair() {
  SharedFD sock1, sock2;
  CHECK(SharedFD::SocketPair(AF_UNIX, SOCK_SEQPACKET, 0, &sock1, &sock2));
  return {UnixMessageSocket(sock1), UnixMessageSocket(sock2)};
}

TEST(UnixMessageSocket, SendPlainMessage) {
  auto [writer, reader] = UnixMessageSocketPair();
  UnixSocketMessage message_in = {{1, 2, 3}, {}};
  auto write_result = writer.WriteMessage(message_in);
  ASSERT_TRUE(write_result.ok()) << write_result.error().Trace();

  auto message_out = reader.ReadMessage();
  ASSERT_TRUE(message_out.ok()) << message_out.error().Trace();
  ASSERT_EQ(message_in.data, message_out->data);
  ASSERT_EQ(0, message_out->control.size());
}

TEST(UnixMessageSocket, SendFileDescriptor) {
  auto [writer, reader] = UnixMessageSocketPair();

  UnixSocketMessage message_in = {{4, 5, 6}, {}};
  auto control_in =
      ControlMessage::FromFileDescriptors({CreateMemFDWithData("abc")});
  ASSERT_TRUE(control_in.ok()) << control_in.error().Trace();
  message_in.control.emplace_back(std::move(*control_in));
  auto write_result = writer.WriteMessage(message_in);
  ASSERT_TRUE(write_result.ok()) << write_result.error().Trace();

  auto message_out = reader.ReadMessage();
  ASSERT_TRUE(message_out.ok()) << message_out.error().Trace();
  ASSERT_EQ(message_in.data, message_out->data);

  ASSERT_EQ(1, message_out->control.size());
  auto fds_out = message_out->control[0].AsSharedFDs();
  ASSERT_TRUE(fds_out.ok()) << fds_out.error().Trace();
  ASSERT_EQ(1, fds_out->size());
  ASSERT_EQ("abc", ReadAllFDData((*fds_out)[0]));
}

TEST(UnixMessageSocket, SendTwoFileDescriptors) {
  auto memfd1 = CreateMemFDWithData("abc");
  auto memfd2 = CreateMemFDWithData("def");

  auto [writer, reader] = UnixMessageSocketPair();
  UnixSocketMessage message_in = {{7, 8, 9}, {}};
  auto control_in = ControlMessage::FromFileDescriptors({memfd1, memfd2});
  ASSERT_TRUE(control_in.ok()) << control_in.error().Trace();
  message_in.control.emplace_back(std::move(*control_in));
  auto write_result = writer.WriteMessage(message_in);
  ASSERT_TRUE(write_result.ok()) << write_result.error().Trace();

  auto message_out = reader.ReadMessage();
  ASSERT_TRUE(message_out.ok()) << message_out.error().Trace();
  ASSERT_EQ(message_in.data, message_out->data);

  ASSERT_EQ(1, message_out->control.size());
  auto fds_out = message_out->control[0].AsSharedFDs();
  ASSERT_TRUE(fds_out.ok()) << fds_out.error().Trace();
  ASSERT_EQ(2, fds_out->size());

  ASSERT_EQ("abc", ReadAllFDData((*fds_out)[0]));
  ASSERT_EQ("def", ReadAllFDData((*fds_out)[1]));
}

TEST(UnixMessageSocket, SendCredentials) {
  auto [writer, reader] = UnixMessageSocketPair();
  auto writer_creds_status = writer.EnableCredentials(true);
  ASSERT_TRUE(writer_creds_status.ok()) << writer_creds_status.error().Trace();
  auto reader_creds_status = reader.EnableCredentials(true);
  ASSERT_TRUE(reader_creds_status.ok()) << reader_creds_status.error().Trace();

  ucred credentials_in;
  credentials_in.pid = getpid();
  credentials_in.uid = getuid();
  credentials_in.gid = getgid();
  UnixSocketMessage message_in = {{1, 5, 9}, {}};
  auto control_in = ControlMessage::FromCredentials(credentials_in);
  message_in.control.emplace_back(std::move(control_in));
  auto write_result = writer.WriteMessage(message_in);
  ASSERT_TRUE(write_result.ok()) << write_result.error().Trace();

  auto message_out = reader.ReadMessage();
  ASSERT_TRUE(message_out.ok()) << message_out.error().Trace();
  ASSERT_EQ(message_in.data, message_out->data);

  ASSERT_EQ(1, message_out->control.size());
  auto credentials_out = message_out->control[0].AsCredentials();
  ASSERT_TRUE(credentials_out.ok()) << credentials_out.error().Trace();
  ASSERT_EQ(credentials_in.pid, credentials_out->pid);
  ASSERT_EQ(credentials_in.uid, credentials_out->uid);
  ASSERT_EQ(credentials_in.gid, credentials_out->gid);
}

TEST(UnixMessageSocket, BadCredentialsBlocked) {
  auto [writer, reader] = UnixMessageSocketPair();
  auto writer_creds_status = writer.EnableCredentials(true);
  ASSERT_TRUE(writer_creds_status.ok()) << writer_creds_status.error().Trace();
  auto reader_creds_status = reader.EnableCredentials(true);
  ASSERT_TRUE(reader_creds_status.ok()) << reader_creds_status.error().Trace();

  ucred credentials_in;
  // This assumes the test is running without root privileges
  credentials_in.pid = getpid() + 1;
  credentials_in.uid = getuid() + 1;
  credentials_in.gid = getgid() + 1;

  UnixSocketMessage message_in = {{2, 4, 6}, {}};
  auto control_in = ControlMessage::FromCredentials(credentials_in);
  message_in.control.emplace_back(std::move(control_in));
  auto write_result = writer.WriteMessage(message_in);
  ASSERT_FALSE(write_result.ok()) << write_result.error().Trace();
}

TEST(UnixMessageSocket, AutoCredentials) {
  auto [writer, reader] = UnixMessageSocketPair();
  auto writer_creds_status = writer.EnableCredentials(true);
  ASSERT_TRUE(writer_creds_status.ok()) << writer_creds_status.error().Trace();
  auto reader_creds_status = reader.EnableCredentials(true);
  ASSERT_TRUE(reader_creds_status.ok()) << reader_creds_status.error().Trace();

  UnixSocketMessage message_in = {{3, 6, 9}, {}};
  auto write_result = writer.WriteMessage(message_in);
  ASSERT_TRUE(write_result.ok()) << write_result.error().Trace();

  auto message_out = reader.ReadMessage();
  ASSERT_TRUE(message_out.ok()) << message_out.error().Trace();
  ASSERT_EQ(message_in.data, message_out->data);

  ASSERT_EQ(1, message_out->control.size());
  auto credentials_out = message_out->control[0].AsCredentials();
  ASSERT_TRUE(credentials_out.ok()) << credentials_out.error().Trace();
  ASSERT_EQ(getpid(), credentials_out->pid);
  ASSERT_EQ(getuid(), credentials_out->uid);
  ASSERT_EQ(getgid(), credentials_out->gid);
}

}  // namespace cuttlefish

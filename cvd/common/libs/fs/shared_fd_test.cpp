/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

#include <stdlib.h>
#include <unistd.h>
#include <gtest/gtest.h>

#include <string>

using avd::InbandMessageHeader;
using avd::SharedFD;

char hello[] = "Hello, world!";
char pipe_message[] = "Testing the pipe";

TEST(SendFD, Basic) {
  char dirname[] = "/tmp/sfdtestXXXXXX";
  char* socket = mkdtemp(dirname);
  EXPECT_TRUE(socket != NULL);
  std::string path(dirname);
  path += "/s";
  SharedFD server = SharedFD::SocketSeqPacketServer(path.c_str(), 0700);
  EXPECT_TRUE(server->IsOpen());
  int rval = fork();
  EXPECT_NE(-1, rval);
  if (!rval) {
    struct iovec iov { hello, sizeof(hello) };
    SharedFD client = SharedFD::SocketSeqPacketClient(path.c_str());
    InbandMessageHeader hdr{};
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    SharedFD fds[2];
    SharedFD::Pipe(fds, fds + 1);
    ssize_t rval = client->SendMsgAndFDs(hdr, 0, fds);
    printf("SendMsg sent %zd (%s)\n", rval, client->StrError());
    exit(0);
  }
  server->Listen(2);
  SharedFD peer = SharedFD::Accept(*server);
  EXPECT_TRUE(peer->IsOpen());
  char buf[80];
  struct iovec iov { buf, sizeof(buf) };
  InbandMessageHeader hdr{};
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  SharedFD fds[2];
  peer->RecvMsgAndFDs(hdr, 0, &fds);
  EXPECT_EQ(0, strcmp(buf, hello));
  EXPECT_TRUE(fds[0]->IsOpen());
  EXPECT_TRUE(fds[1]->IsOpen());
  EXPECT_EQ(sizeof(pipe_message), fds[1]->Write(pipe_message, sizeof(pipe_message)));
  EXPECT_EQ(sizeof(pipe_message), fds[0]->Read(buf, sizeof(buf)));
  EXPECT_EQ(0, strcmp(buf, pipe_message));
}

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

using cvd::SharedFD;

char pipe_message[] = "Testing the pipe";

TEST(SendFD, Basic) {
  SharedFD fds[2];
  SharedFD::Pipe(fds, fds + 1);
  EXPECT_TRUE(fds[0]->IsOpen());
  EXPECT_TRUE(fds[1]->IsOpen());
  EXPECT_EQ(sizeof(pipe_message), fds[1]->Write(pipe_message, sizeof(pipe_message)));
  char buf[80];
  EXPECT_EQ(sizeof(pipe_message), fds[0]->Read(buf, sizeof(buf)));
  EXPECT_EQ(0, strcmp(buf, pipe_message));
}

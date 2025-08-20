/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "cuttlefish/host/commands/cvdalloc/sem.h"

#include "cuttlefish/host/libs/command_util/util.h"

namespace cuttlefish {

Result<void> Post(const SharedFD socket) {
  char i = 0;
  CF_EXPECT(socket->Write(&i, sizeof(char)) != -1);
  return {};
}

Result<void> Wait(const SharedFD socket, std::chrono::seconds timeout) {
  CF_EXPECT(socket->IsOpen(), "IsOpen: " << socket->StrError());
  CF_EXPECT(WaitForRead(socket, timeout.count()), "WaitForRead");
  char i = -1;
  int r = socket->Read(&i, sizeof(i));
  CF_EXPECT_GT(r, 0, socket->StrError());
  CF_EXPECT(i == 0, "Unexpected read");
  return {};
}

}  // namespace

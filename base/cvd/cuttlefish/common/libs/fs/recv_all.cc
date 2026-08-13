/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/common/libs/fs/recv_all.h"

#include <stdint.h>

#include <memory>
#include <string>

#include "cuttlefish/common/libs/fs/file_instance.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::string> RecvAll(FileInstance& sock, const uint64_t count) {
  uint64_t total_read{};
  CF_EXPECT(sock.IsOpen());
  std::unique_ptr<char[]> data(new char[count]);
  while (total_read < count) {
    total_read +=
        CF_EXPECT(sock.Read(data.get() + total_read, count - total_read));
  }
  return std::string(data.get(), count);
}

}  // namespace cuttlefish

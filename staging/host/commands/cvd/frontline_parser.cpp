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

#include "host/commands/cvd/frontline_parser.h"

#include <sstream>
#include <vector>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

Result<Json::Value> FrontlineParser::ListSubcommands() {
  std::vector<std::string> args{"cvd", "cmd-list"};
  SharedFD read_pipe, write_pipe;
  CF_EXPECT(cuttlefish::SharedFD::Pipe(&read_pipe, &write_pipe),
            "Unable to create shutdown pipe: " << strerror(errno));
  OverrideFd new_control_fd{.stdout_override_fd = write_pipe};
  CF_EXPECT(client_.HandleCommand(args, envs_, std::vector<std::string>{},
                                  new_control_fd));

  write_pipe->Close();
  const int kChunkSize = 512;
  char buf[kChunkSize + 1] = {0};
  std::stringstream ss;
  do {
    auto n_read = ReadExact(read_pipe, buf, kChunkSize);
    CF_EXPECT(n_read >= 0 && (n_read <= kChunkSize));
    if (n_read == 0) {
      break;
    }
    buf[n_read] = 0;  // null-terminate the C-style string
    ss << buf;
    if (n_read < sizeof(buf) - 1) {
      break;
    }
  } while (true);
  auto json_output = CF_EXPECT(ParseJson(ss.str()));
  return json_output;
}

}  // namespace cuttlefish

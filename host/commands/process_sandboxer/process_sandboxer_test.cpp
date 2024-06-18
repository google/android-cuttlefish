//
// Copyright (C) 2024 The Android Open Source Project
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

#include <libgen.h>
#include <stdlib.h>

#include <string>
#include <string_view>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "common/libs/utils/subprocess.h"

namespace cuttlefish {

static std::string ExecutableSelfPath() {
  char exe_path[PATH_MAX + 1];
  auto path_size = readlink("/proc/self/exe", exe_path, PATH_MAX);
  CHECK_GT(path_size, 0) << strerror(errno);
  CHECK_LE(path_size, PATH_MAX);
  exe_path[path_size + 1] = '\0';  // Readlink does not append a null terminator
  char abs_path[PATH_MAX];
  char* real = realpath(exe_path, abs_path);
  CHECK(real) << strerror(errno);
  return real;
}

static std::string HostArtifactsDir() {
  auto path = ExecutableSelfPath();
  path = dirname(path.data());  // x86_64
  path = dirname(path.data());  // <test case>
  path = dirname(path.data());  // testcases
  path = dirname(path.data());  // linux-86
  return path;
}

static std::string ExecutablePath(std::string_view exe) {
  auto dir_path = ExecutableSelfPath();
  dir_path = dirname(dir_path.data());
  return fmt::format("{}/{}", dir_path, exe);
}

TEST(SandboxExecutable, HelloWorld) {
  Command executable(ExecutablePath("process_sandboxer_test_hello_world"));
  auto opt = SubprocessOptions().SandboxArguments({
      ExecutablePath("process_sandboxer"),
      "--host_artifacts_path=" + HostArtifactsDir(),
  });

  std::string in, out, err;
  int code = RunWithManagedStdio(std::move(executable), &in, &out, &err, opt);

  EXPECT_EQ(code, 0) << err;
  EXPECT_EQ(out, "Allocated vector with 100 members\n");
}

}  // namespace cuttlefish

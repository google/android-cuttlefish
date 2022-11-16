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

#include "host/commands/cvd/unittests/selector/creation_analyzer_test_helper.h"

#include <android-base/strings.h>

namespace cuttlefish {
namespace selector {

CreationInfoGenTest::CreationInfoGenTest() { Init(); }
void CreationInfoGenTest::Init() {
  const auto& input_param = GetParam();
  selector_args_ = android::base::Tokenize(input_param.selector_args, " ");
  cmd_args_ = android::base::Tokenize(input_param.cmd_args, " ");
  if (!input_param.home.empty()) {
    envs_["HOME"] = input_param.home;
  }
  if (!input_param.android_host_out.empty()) {
    envs_["ANDROID_HOST_OUT"] = input_param.android_host_out;
  }
  expected_output_ = input_param.expected_output.output;
  expected_success_ = input_param.expected_output.is_success;
  credential_ = ucred{.pid = getpid(), .uid = getuid(), .gid = getgid()};
}

}  // namespace selector
}  // namespace cuttlefish

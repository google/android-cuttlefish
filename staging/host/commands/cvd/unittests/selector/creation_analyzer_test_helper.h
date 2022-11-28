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

#pragma once

#include <sys/types.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "host/commands/cvd/selector/creation_analyzer.h"

namespace cuttlefish {
namespace selector {

using Envs = std::unordered_map<std::string, std::string>;
using Args = std::vector<std::string>;

struct OutputInfo {
  std::string home;
  std::string host_artifacts_path;  ///< e.g. out/host/linux-x86
  std::string group_name;
  std::vector<unsigned> instances;
  std::vector<std::string> args;
  std::unordered_map<std::string, std::string> envs;
};

struct Expected {
  OutputInfo output;
  bool is_success;
};

struct InputOutput {
  // inputs
  std::string selector_args;
  std::string cmd_args;
  std::string home;
  std::string android_host_out;

  // output
  Expected expected_output;
};

class CreationInfoGenTest : public testing::TestWithParam<InputOutput> {
 protected:
  CreationInfoGenTest();
  void Init();

  std::vector<std::string> selector_args_;
  std::string sub_cmd_;
  std::vector<std::string> cmd_args_;
  std::unordered_map<std::string, std::string> envs_;
  std::optional<ucred> credential_;
  OutputInfo expected_output_;
  bool expected_success_;
  InstanceDatabase instance_db_;
  InstanceLockFileManager instance_lock_file_manager_;
};

class HomeTest : public CreationInfoGenTest {};
class HostArtifactsTest : public CreationInfoGenTest {};
class InvalidSubCmdTest : public CreationInfoGenTest {};
class ValidSubCmdTest : public CreationInfoGenTest {};

}  // namespace selector
}  // namespace cuttlefish

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

#include "host/commands/cvd/unittests/instance_database_test_helper.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/subprocess.h"

namespace cuttlefish {
namespace instance_db {
namespace unittest {
namespace {

template <typename... Args>
void RunCmd(const std::string& exe_path, Args&&... args) {
  Command command(exe_path);
  auto add_param = [&command](const std::string& arg) {
    command.AddParameter(arg);
  };
  (add_param(std::forward<Args>(args)), ...);
  command.Start().Wait();
}

std::string GetRandomInstanceName(const unsigned long len) {
  std::string alphabets{
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "_"};
  std::string token;
  std::sample(alphabets.begin(), alphabets.end(), std::back_inserter(token),
              std::min(len, alphabets.size()),
              std::mt19937{std::random_device{}()});
  return token;
}

}  // namespace

DbTester::DbTester()
    : android_host_out_(StringFromEnv("ANDROID_HOST_OUT", ".")) {
  Clear();
  SetUp();
}

DbTester::~DbTester() { Clear(); }

void DbTester::Clear() {
  if (!tmp_dir_.empty()) {
    RunCmd("/usr/bin/rm", "-fr", tmp_dir_);
  }
  fake_homes_.clear();
}

void DbTester::SetUp() {
  char temp_dir_name_pattern[] = "/tmp/cf_unittest.XXXXXX";
  // temp_dir_name_pattern will be modified by mkdtemp call
  auto ptr = mkdtemp(temp_dir_name_pattern);
  tmp_dir_ = (!ptr ? "/tmp/cf_unittest/default_location" : ptr);
  for (int i = 1; i < kNGroups + 1; i++) {
    std::string subdir = tmp_dir_ + "/cf" + std::to_string(i);
    fake_homes_.emplace_back(subdir);
    RunCmd("/usr/bin/mkdir", subdir);
  }
}

std::vector<std::unordered_set<std::string>> DbTester::InstanceNames(
    const int n_groups) const {
  int n_instances = 1;
  std::vector<std::unordered_set<std::string>> result;
  for (int group = 0; group < n_groups; group++) {
    std::unordered_set<std::string> instances;
    for (int i = 0; i < n_instances; i++) {
      std::string prefix(1, 'a' + i);
      instances.insert(prefix + "_" + GetRandomInstanceName(5));
    }
    result.emplace_back(std::move(instances));
    n_instances++;  // one more instance in the next loop
  }
  return result;
}

}  // namespace unittest
}  // namespace instance_db
}  // namespace cuttlefish

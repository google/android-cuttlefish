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

#include <string>
#include <unordered_set>
#include <vector>

#include "host/commands/cvd/instance_database.h"

namespace cuttlefish {
namespace instance_db {
namespace unittest {

class DbTester {
 public:
  DbTester();
  ~DbTester();
  const std::vector<std::string>& Homes() const { return fake_homes_; }
  const std::string& HostOutDir() const { return android_host_out_; }
  InstanceDatabase& Db() { return db_; }
  std::vector<std::unordered_set<std::string>> InstanceNames(
      const int n_groups) const;
  static constexpr const unsigned kNGroups = 10;

 private:
  void SetUp();
  void Clear();
  std::vector<std::string> fake_homes_;
  std::string android_host_out_;
  std::string tmp_dir_;
  InstanceDatabase db_;
};

}  // namespace unittest
}  // namespace instance_db
}  // namespace cuttlefish

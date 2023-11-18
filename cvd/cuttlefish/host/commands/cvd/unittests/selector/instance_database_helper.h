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

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "host/commands/cvd/selector/constant_reference.h"
#include "host/commands/cvd/selector/instance_database.h"

namespace cuttlefish {
namespace selector {

/**
 * Creates n mock HOME directories, one per group. Also, creates
 * 1 mock ANDROID_HOST_OUT with a mock launcher file.
 *
 * The test suite is to assess InstanceDatabase APIs such as
 * adding groups, adding instances to the groups, etc. The thing
 * is that the InstanceDatabase APIs will check if HOME and/or
 * ANDROID_HOST_OUT are directories. Also, for ANDROID_HOST_OUT,
 * as a bare minimum validity check, it will see if there is a launcher
 * file under the bin directory of it.
 *
 * Thus, the mock environment should prepare an actual directories with
 * a mock launcher file(s). In case the test runs/tests in the suite run
 * in parallel, we give each test run a unique directory, and that's why
 * all mock homes are under a temp directory created by mkdtemp()
 *
 */
class CvdInstanceDatabaseTest : public ::testing::Test {
 protected:
  enum class ErrorCode : std::int32_t {
    kOk,
    kFileError,
    kInstanceDabaseError,
  };

  struct SetupError {
    ErrorCode error_code;
    std::string msg;
  };

  CvdInstanceDatabaseTest();
  ~CvdInstanceDatabaseTest();

  bool SetUpOk() const { return error_.error_code == ErrorCode::kOk; }
  const std::string& Workspace() const { return workspace_dir_; }
  /*
   * Returns a valid host artifacts dir, which is a prerequisite for
   * InstanceDatabase APIs.
   */
  const std::string& HostArtifactsPath() const {
    return android_artifacts_path_;
  }

  // Adds InstanceGroups, each by:
  //    "mkdir" : Workspace() + "/" + base_name, HostArtifactsPath()
  //    db_.AddInstanceGroup()
  bool AddGroups(const std::unordered_set<std::string>& base_names);
  struct InstanceInfo {
    unsigned id;
    std::string per_instance_name;
  };
  bool AddInstances(const std::string& group_name,
                    const std::vector<InstanceInfo>& instances_info);
  InstanceDatabase& GetDb() { return db_; }
  const SetupError& Error() const { return error_; }

 private:
  void ClearWorkspace();
  bool InitWorkspace();
  bool InitMockAndroidHostOut();
  // set error_ when there is an error
  void SetErrorCode(const ErrorCode error_code, const std::string& msg);

  std::string android_artifacts_path_;
  std::string workspace_dir_;
  SetupError error_;
  InstanceDatabase db_;
};

using CvdInstanceDatabaseJsonTest = CvdInstanceDatabaseTest;

}  // namespace selector
}  // namespace cuttlefish

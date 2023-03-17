//
// Copyright (C) 2023 The Android Open Source Project
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

namespace cuttlefish {
namespace acloud {

/**
 * Creates a mock cmd_ command line, and execute the test flow
 *
 */
class CvdInstanceLocalTest : public ::testing::Test {
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

  CvdInstanceLocalTest();
  ~CvdInstanceLocalTest();

  bool SetUpOk() const { return error_.error_code == ErrorCode::kOk; }

  const SetupError& Error() const { return error_; }

  CmdResult Execute(const std::string& cmd_);

 private:
  bool InitCmd();
  // set error_ when there is an error
  void SetErrorCode(const ErrorCode error_code, const std::string& msg);
  SetupError error_;
};

}  // namespace acloud
}  // namespace cuttlefish

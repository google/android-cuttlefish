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

#include "common/libs/utils/files_test_helper.h"

namespace cuttlefish {

EmulateAbsolutePathBase::EmulateAbsolutePathBase() {
  input_path_ = GetParam().path_to_convert_;
  expected_path_ = GetParam().expected_;
}

EmulateAbsolutePathWithPwd::EmulateAbsolutePathWithPwd() {
  input_path_ = GetParam().path_to_convert_;
  expected_path_ = GetParam().expected_;
  current_dir_ = GetParam().working_dir_;
}

EmulateAbsolutePathWithHome::EmulateAbsolutePathWithHome() {
  input_path_ = GetParam().path_to_convert_;
  expected_path_ = GetParam().expected_;
  home_dir_ = GetParam().home_dir_;
}

}  // namespace cuttlefish

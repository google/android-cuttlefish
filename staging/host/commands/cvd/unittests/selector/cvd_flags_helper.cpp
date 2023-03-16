/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/unittests/selector/cvd_flags_helper.h"

#include <cstdint>

#include <android-base/strings.h>

namespace cuttlefish {

CvdFlagCollectionTest::CvdFlagCollectionTest() {
  std::string in_str = "--help --name=foo --not_consumed --enable_vnc --id 9";
  input_ = android::base::Tokenize(in_str, " ");
  CvdFlag<bool> Help("help", false);
  CvdFlag<std::string> Name("name");
  CvdFlag<bool> EnableVnc("enable_vnc", true);
  CvdFlag<std::int32_t> Id("id");
  CvdFlag<bool> NotGiven("not-given");

  flag_collection_.EnrollFlag(std::move(Help));
  flag_collection_.EnrollFlag(std::move(Name));
  flag_collection_.EnrollFlag(std::move(EnableVnc));
  flag_collection_.EnrollFlag(std::move(Id));
  flag_collection_.EnrollFlag(std::move(NotGiven));
}

}  // namespace cuttlefish

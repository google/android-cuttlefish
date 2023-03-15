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

#pragma once

#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/flag.h"

namespace cuttlefish {

/**
 * The authentic collection of cvd driver flags
 *
 */
// names of the flags, which are also used for search

class DriverFlags {
 public:
  static constexpr char kClean[] = "clean";
  static constexpr char kHelp[] = "help";
  static const DriverFlags& Get();

  Result<CvdFlagProxy> GetFlag(const std::string& search_key) const {
    auto flag = CF_EXPECT(flags_.GetFlag(search_key));
    return flag;
  }

  std::vector<CvdFlagProxy> Flags() const { return flags_.Flags(); }

 private:
  DriverFlags() {
    flags_.EnrollFlag(CleanFlag(kClean, false));
    flags_.EnrollFlag(HelpFlag());
  }
  CvdFlag<bool> CleanFlag(const std::string& name, const bool default_val);
  CvdFlag<bool> HelpFlag();

  FlagCollection flags_;
};

}  // namespace cuttlefish

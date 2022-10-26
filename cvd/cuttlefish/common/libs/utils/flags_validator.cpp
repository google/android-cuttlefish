/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "common/libs/utils/flags_validator.h"

namespace cuttlefish {
Result<void> ValidateStupWizardMode(const std::string& setupwizard_mode) {
  // One of DISABLED,OPTIONAL,REQUIRED
  bool result = setupwizard_mode == "DISABLED" ||
                setupwizard_mode == "OPTIONAL" ||
                setupwizard_mode == "REQUIRED";

  CF_EXPECT(result == true, "Invalid value for setupwizard_mode config");
  return {};
}

}  // namespace cuttlefish

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

#include "host/commands/cvd/driver_flags.h"

#include <sstream>

namespace cuttlefish {

CvdFlag<bool> DriverFlags::HelpFlag() {
  const bool default_val = false;
  CvdFlag<bool> help_flag(kHelp, default_val);
  std::stringstream help;
  help << "--" << kHelp << "to print this message.";
  help_flag.SetHelpMessage(help.str());
  return help_flag;
}

}  // namespace cuttlefish

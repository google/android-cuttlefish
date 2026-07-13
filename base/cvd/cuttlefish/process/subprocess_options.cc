/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/process/subprocess_options.h"

#include <string>
#include <utility>

namespace cuttlefish {

SubprocessOptions& SubprocessOptions::Verbose(bool verbose) & {
  verbose_ = verbose;
  return *this;
}
SubprocessOptions SubprocessOptions::Verbose(bool verbose) && {
  verbose_ = verbose;
  return std::move(*this);
}

#ifdef __linux__
SubprocessOptions& SubprocessOptions::ExitWithParent(bool exit_with_parent) & {
  exit_with_parent_ = exit_with_parent;
  return *this;
}
SubprocessOptions SubprocessOptions::ExitWithParent(bool exit_with_parent) && {
  exit_with_parent_ = exit_with_parent;
  return std::move(*this);
}
#endif

SubprocessOptions& SubprocessOptions::InGroup(bool in_group) & {
  in_group_ = in_group;
  return *this;
}
SubprocessOptions SubprocessOptions::InGroup(bool in_group) && {
  in_group_ = in_group;
  return std::move(*this);
}

SubprocessOptions& SubprocessOptions::Strace(std::string s) & {
  strace_ = std::move(s);
  return *this;
}
SubprocessOptions SubprocessOptions::Strace(std::string s) && {
  strace_ = std::move(s);
  return std::move(*this);
}

}  // namespace cuttlefish

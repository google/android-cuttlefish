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
#pragma once

#include <string>

namespace cuttlefish {

class SubprocessOptions {
 public:
  SubprocessOptions()
      : verbose_(true), exit_with_parent_(true), in_group_(false) {}
  SubprocessOptions& Verbose(bool verbose) &;
  SubprocessOptions Verbose(bool verbose) &&;
#ifdef __linux__
  SubprocessOptions& ExitWithParent(bool exit_with_parent) &;
  SubprocessOptions ExitWithParent(bool exit_with_parent) &&;
#endif
  // The subprocess runs as head of its own process group.
  SubprocessOptions& InGroup(bool in_group) &;
  SubprocessOptions InGroup(bool in_group) &&;

  SubprocessOptions& Strace(std::string strace_output_path) &;
  SubprocessOptions Strace(std::string strace_output_path) &&;

  bool Verbose() const { return verbose_; }
  bool ExitWithParent() const { return exit_with_parent_; }
  bool InGroup() const { return in_group_; }
  const std::string& Strace() const { return strace_; }

 private:
  bool verbose_;
  bool exit_with_parent_;
  bool in_group_;
  std::string strace_;
};

}  // namespace cuttlefish

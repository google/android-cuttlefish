//
// Copyright (C) 2019 The Android Open Source Project
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

#include <memory>
#include <set>
#include <string>
#include <vector>

class SubprocessFlag;

class FlagForwarder {
  std::set<std::string> subprocesses_;
  std::set<std::unique_ptr<SubprocessFlag>> flags_;

public:
  FlagForwarder(std::set<std::string> subprocesses);
  ~FlagForwarder();
  FlagForwarder(FlagForwarder&&) = default;
  FlagForwarder(const FlagForwarder&) = delete;
  FlagForwarder& operator=(FlagForwarder&&) = default;
  FlagForwarder& operator=(const FlagForwarder&) = delete;

  void UpdateFlagDefaults() const;
  std::vector<std::string> ArgvForSubprocess(
      const std::string& subprocess,
      const std::vector<std::string>& args = std::vector<std::string>()) const;
};

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

#include <cstdint>
#include <set>
#include <string>

#include "common/libs/utils/result.h"

namespace cuttlefish {

class InstanceNumsCalculator {
 public:
  InstanceNumsCalculator& FromFlags(const std::vector<std::string>&) &;
  InstanceNumsCalculator FromFlags(const std::vector<std::string>&) &&;

  InstanceNumsCalculator& FromGlobalGflags() &;
  InstanceNumsCalculator FromGlobalGflags() &&;

  InstanceNumsCalculator& BaseInstanceNum(std::int32_t) &;
  InstanceNumsCalculator BaseInstanceNum(std::int32_t) &&;

  InstanceNumsCalculator& NumInstances(std::int32_t) &;
  InstanceNumsCalculator NumInstances(std::int32_t) &&;

  InstanceNumsCalculator& InstanceNums(const std::string&) &;
  InstanceNumsCalculator InstanceNums(const std::string&) &&;

  InstanceNumsCalculator& InstanceNums(std::set<std::int32_t>) &;
  InstanceNumsCalculator InstanceNums(std::set<std::int32_t>) &&;

  Result<std::set<std::int32_t>> Calculate();

 private:
  template <typename T>
  void TrySet(T& field, Result<T> result);

  Result<void> setter_result_;
  std::optional<std::int32_t> base_instance_num_;
  std::optional<std::int32_t> num_instances_;
  std::set<std::int32_t> instance_nums_;
};

}  // namespace cuttlefish

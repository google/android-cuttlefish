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

#include <fruit/fruit.h>
#include <vector>

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {

class CommandSource : public virtual SetupFeature {
 public:
  virtual ~CommandSource() = default;
  virtual std::vector<Command> Commands() = 0;
};

}  // namespace cuttlefish

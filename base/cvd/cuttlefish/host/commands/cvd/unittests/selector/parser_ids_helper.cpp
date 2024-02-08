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

#include "host/commands/cvd/unittests/selector/parser_ids_helper.h"

#include <sys/types.h>
#include <unistd.h>

#include <android-base/strings.h>

#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace selector {

InstanceIdTest::InstanceIdTest() {
  auto cuttlefish_instance = GetParam().cuttlefish_instance;
  if (cuttlefish_instance) {
    envs_[kCuttlefishInstanceEnvVarName] = cuttlefish_instance.value();
  }
  cmd_args_ = android::base::Tokenize(GetParam().cmd_args, " ");
  selector_args_ = android::base::Tokenize(GetParam().selector_args, " ");
  expected_ids_ = GetParam().expected_ids;
  expected_result_ = GetParam().expected_result;
  requested_num_instances_ = GetParam().requested_num_instances;
}

}  // namespace selector
}  // namespace cuttlefish

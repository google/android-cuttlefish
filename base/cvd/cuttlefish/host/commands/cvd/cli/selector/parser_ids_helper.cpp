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

#include "cuttlefish/host/commands/cvd/cli/selector/parser_ids_helper.h"

#include <sys/types.h>
#include <unistd.h>

#include "absl/strings/str_split.h"

namespace cuttlefish {
namespace selector {

InstanceIdTest::InstanceIdTest() {
  cmd_args_ = absl::StrSplit(GetParam().cmd_args, ' ', absl::SkipEmpty());
  selector_opts_ = GetParam().selector_opts;
  expected_ids_ = GetParam().expected_ids;
  expected_result_ = GetParam().expected_result;
  requested_num_instances_ = GetParam().requested_num_instances;
}

}  // namespace selector
}  // namespace cuttlefish

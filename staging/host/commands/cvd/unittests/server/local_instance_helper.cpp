//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/unittests/server/local_instance_helper.h"

#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace acloud {

CvdInstanceLocalTest::CvdInstanceLocalTest() { InitCmd(); }

CmdResult CvdInstanceLocalTest::Execute(const std::string& cmd_) {
  cvd_common::Envs envs;
  CmdResult result = CmdRunner::Run(cmd_, envs);

  auto cmd_stop = CmdRunner::Run("cvd stop", envs);
  // clean up for the next test
  CmdRunner::Run("cvd reset -y", envs);

  return result;
}

void CvdInstanceLocalTest::InitCmd() {
  cvd_common::Envs envs;
  CmdRunner::Run("cvd reset -y", envs);
}

}  // namespace acloud
}  // namespace cuttlefish

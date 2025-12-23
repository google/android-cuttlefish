/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <cstdlib>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

DEFINE_int32(instance_num, cuttlefish::GetInstance(),
             "Which instance to trigger a power button event.");

namespace cuttlefish {
namespace {

Result<void> PowerbtnCvdMain() {
  const CuttlefishConfig* config =
      CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");
  auto instance = config->ForInstance(FLAGS_instance_num);

  Command command = Command(instance.crosvm_binary())
                        .AddParameter("powerbtn")
                        .AddParameter(instance.CrosvmSocketPath());

  LOG(INFO) << "Pressing power button";
  CF_EXPECT(RunAndCaptureStdout(std::move(command)));
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);
  cuttlefish::Result<void> result = cuttlefish::PowerbtnCvdMain();
  if (!result.ok()) {
    LOG(ERROR) << result.error().FormatForEnv();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

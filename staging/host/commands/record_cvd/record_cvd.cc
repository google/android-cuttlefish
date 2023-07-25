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

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace {

Result<void> RecordCvdMain() {
  LOG(INFO) << "Recording operation are unimplemented.";
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::Result<void> result = cuttlefish::RecordCvdMain();
  if (!result.ok()) {
    LOG(ERROR) << result.error().Message();
    LOG(DEBUG) << result.error().Trace();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

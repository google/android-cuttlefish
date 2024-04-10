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

#include <stdlib.h>
#include <unistd.h>

#include "android-base/logging.h"

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"

namespace cuttlefish {

namespace {

constexpr char kCvdBinaryPath[] = "/usr/bin/cvd";

int FallbackToPythonAcloud(char** argv) {
  auto android_top = StringFromEnv("ANDROID_BUILD_TOP", "");
  if (android_top == "") {
    LOG(FATAL) << "Could not find android environment. Please run "
               << "\"source build/envsetup.sh\".";
    abort();
  }
  // TODO(b/206893146): Detect what the platform actually is.
  auto py_acloud_path =
      android_top + "/prebuilts/asuite/acloud/linux-x86/acloud";

  return execv(py_acloud_path.c_str(), argv);
}

int TranslatorMain(char** argv) {
  if (!FileExists(kCvdBinaryPath, true /*follow symlinks*/)) {
    LOG(WARNING)
        << "The host packages may not be installed or are old, "
           "consider running `acloud setup --host` to get the latest features.";
    return FallbackToPythonAcloud(argv);
  }

  // This executes /usr/bin/cvd with argv[0] = "acloud", which triggers the
  // translator flow and can still fallback to the python prebuilt if needed
  // using the environment.
  return execv(kCvdBinaryPath, argv);
}

}  // namespace

}  // namespace cuttlefish

int main(int, char** argv) {
  android::base::InitLogging(argv, android::base::StderrLogger);
  return cuttlefish::TranslatorMain(argv);
}

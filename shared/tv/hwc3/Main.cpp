/*
 * Copyright 2022, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <sched.h>

#include "Composer.h"

using aidl::android::hardware::graphics::composer3::impl::Composer;
using aidl::android::hardware::graphics::composer3::impl::
    PictureProfileChangedListener;

int main(int /*argc*/, char** /*argv*/) {
  ALOGI("RanchuHWC.TV (HWComposer3/HWC3) starting up...");

  // same as SF main thread
  struct sched_param param = {0};
  param.sched_priority = 2;
  if (sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
    ALOGE("%s: failed to set priority: %s", __FUNCTION__, strerror(errno));
  }

  auto composer = ndk::SharedRefBase::make<Composer>();
  CHECK(composer != nullptr);

  const std::string composerInstance =
      std::string() + Composer::descriptor + "/default";
  binder_status_t composerStatus = AServiceManager_addService(
      composer->asBinder().get(), composerInstance.c_str());
  CHECK(composerStatus == STATUS_OK);

  ALOGI("Finding IPictureProfileChangedListener declaration");
  if (PictureProfileChangedListener::isDeclared()) {
    ALOGI("Found IPictureProfileChangedListener declaration");
    auto pictureProfileChangedListener =
        composer->getPictureProfileChangedListener();
    CHECK(pictureProfileChangedListener != nullptr);
    const std::string ppclInstance =
        std::string() + PictureProfileChangedListener::descriptor + "/default";
    binder_status_t ppclStatus = AServiceManager_addService(
        pictureProfileChangedListener->asBinder().get(), ppclInstance.c_str());
    CHECK(ppclStatus == STATUS_OK);
  }

  // Thread pool for system libbinder (via libbinder_ndk) for aidl services
  // IComposer and IDisplay
  ABinderProcess_setThreadPoolMaxThreadCount(5);
  ABinderProcess_startThreadPool();
  ABinderProcess_joinThreadPool();

  return EXIT_FAILURE;
}

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

#include <aidl/com/android/minidroid/testservice/ITestService.h>
#include <minidroid_sd.h>

#include <stdio.h>
#include <unistd.h>

#define LOG_TAG "client_minidroid"
#include <log/log.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    LOG_FATAL(
        "Wrong usage of ITestService client. Please enter the CID and port of "
        "the proxy process!");
    return -1;
  }

  int service_host_cid = atoi(argv[1]);
  int service_port = atoi(argv[2]);

  ALOGI("Hello Minidroid client! Connecting to CID %d and port %d",
        service_host_cid, service_port);

  ndk::SpAIBinder binder = bi::sd::getService(service_host_cid, service_port);

  if (nullptr == binder.get()) {
    LOG_FATAL("Unable to find service!");
    return -1;
  }

  auto test_service =
      aidl::com::android::minidroid::testservice::ITestService::fromBinder(
          binder);

  test_service->sayHello();
  test_service->printText("Hello from client!");
  int32_t result = 0;
  test_service->addInteger(4, 6, &result);

  ALOGI("Finished client. 4 + 6 is %d", result);

  return 0;
}

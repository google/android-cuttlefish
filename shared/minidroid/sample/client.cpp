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
#include <android/binder_auto_utils.h>
#include <binder_rpc_unstable.hpp>

#include <stdio.h>
#include <unistd.h>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) {
    printf(
        "Wrong usage of ITestService client. Please enter the CID of the "
        "service host VM!\n");
    return -1;
  }

  int service_host_cid = atoi(argv[1]);
  int service_port =
      aidl::com::android::minidroid::testservice::ITestService::SERVICE_PORT;

  printf("Hello Minidroid client! Connecting to CID %d and port %d\n",
         service_host_cid, service_port);

  AIBinder* service = ARpcSession_setupVsockClient(
      ARpcSession_new(), service_host_cid, service_port);
  ndk::SpAIBinder binder(service);

  auto test_service =
      aidl::com::android::minidroid::testservice::ITestService::fromBinder(
          binder);

  test_service->sayHello();
  test_service->printText("Hello from client!");
  int32_t result = 0;
  test_service->addInteger(4, 6, &result);

  printf("Finished client. 4 + 6 is %d\n", result);

  return 0;
}

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

#include "include/minidroid_sd.h"

#include <sys/socket.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <linux/vm_sockets.h>
#include <stdio.h>
#include <binder_rpc_unstable.hpp>

void bi::sd::setupRpcServer(ndk::SpAIBinder service, int port) {
  ABinderProcess_startThreadPool();
  ARpcServer* server = ARpcServer_newVsock(service.get(), VMADDR_CID_ANY, port);

  AServiceManager_addService(service.get(), "TestService");
  printf("Calling join on server!\n");
  ARpcServer_join(server);
}

ndk::SpAIBinder bi::sd::getService(int cid, int port) {
  return ndk::SpAIBinder(
      ARpcSession_setupVsockClient(ARpcSession_new(), cid, port));
}

/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <api_level_fixes.h>
#include <UniquePtr.h>

#include "logging.h"
#include "namespace_aware_executor.h"
#include "netlink_client.h"
#include "network_interface_manager.h"
#include "network_namespace_manager.h"
#include "sys_client.h"

using avd::UniquePtr;
using avd::NamespaceAwareExecutor;
using avd::NetlinkClient;
using avd::NetworkInterfaceManager;
using avd::NetworkNamespaceManager;
using avd::SysClient;

namespace {
const char* kNewShellInNamespace[] = {
  "echo New session started in requested namespace.",
  "echo Press ^D to return to previous session.",
  "/system/bin/sh",
  NULL
};
}  // namespace

int main(int argc, char** argv) {
  if (argc == 1) {
    // No parameters. Do nothing.
    return 0;
  }

#if GCE_PLATFORM_SDK_BEFORE(J_MR2)
  klog_init();
#endif
  klog_set_level(KLOG_INFO_LEVEL);

  UniquePtr<SysClient> sys_client(SysClient::New());
  if (!sys_client.get()) return 1;

  UniquePtr<NetlinkClient> nl_client(NetlinkClient::New(sys_client.get()));
  if (!nl_client.get()) return 1;

  UniquePtr<NetworkNamespaceManager> ns_manager(
      NetworkNamespaceManager::New(sys_client.get()));
  if (!ns_manager.get()) return 1;

  UniquePtr<NetworkInterfaceManager> if_manager(
      NetworkInterfaceManager::New(nl_client.get(), ns_manager.get()));
  if (!if_manager.get()) return 1;

  UniquePtr<NamespaceAwareExecutor> executor(
      NamespaceAwareExecutor::New(ns_manager.get(), sys_client.get()));
  if (!executor.get()) return 1;

  std::string command(argv[1]);
  if (command == "nsexec") {
    if (argc < 3) {
      KLOG_ERROR(LOG_TAG, "nsexec: too few parameters.\n");
      return 1;
    }

    std::string net_ns = argv[2];
    if (argc > 3) {
      std::string command;
      for (int index = 3; index < argc; ++index) {
        command += argv[index];
        command += " ";
      }
      const char* commands[] = {
        command.c_str(),
        NULL
      };
      if (!executor->Execute(net_ns, true, commands)) return 1;
    } else {
      if (!executor->Execute(net_ns, true, kNewShellInNamespace)) return 1;
    }
  } else {
    // No help yet.
    KLOG_ERROR(LOG_TAG, "unknown command: %s", argv[2]);
    return 1;
  }

  return 0;
}


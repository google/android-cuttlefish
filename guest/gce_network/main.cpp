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
#include <memory>

#include <glog/logging.h>

#include "guest/gce_network/namespace_aware_executor.h"
#include "guest/gce_network/netlink_client.h"
#include "guest/gce_network/network_interface_manager.h"
#include "guest/gce_network/network_namespace_manager.h"
#include "guest/gce_network/sys_client.h"

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

  google::InitGoogleLogging();
  google::LogToStderr();

  std::unique_ptr<SysClient> sys_client(SysClient::New());
  if (!sys_client.get()) return 1;

  std::unique_ptr<NetlinkClient> nl_client(NetlinkClient::New(sys_client.get()));
  if (!nl_client.get()) return 1;

  std::unique_ptr<NetworkNamespaceManager> ns_manager(
      NetworkNamespaceManager::New(sys_client.get()));
  if (!ns_manager.get()) return 1;

  std::unique_ptr<NetworkInterfaceManager> if_manager(
      NetworkInterfaceManager::New(nl_client.get(), ns_manager.get()));
  if (!if_manager.get()) return 1;

  std::unique_ptr<NamespaceAwareExecutor> executor(
      NamespaceAwareExecutor::New(ns_manager.get(), sys_client.get()));
  if (!executor.get()) return 1;

  std::string command(argv[1]);
  if (command == "nsexec") {
    if (argc < 3) {
      LOG(ERROR) << "nsexec: too few parameters.";
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
    LOG(ERROR) << "unknown command: " << argv[2];
    return 1;
  }

  return 0;
}


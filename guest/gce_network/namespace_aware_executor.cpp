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

#include "namespace_aware_executor.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

namespace avd {

NamespaceAwareExecutor::NamespaceAwareExecutor(
      NetworkNamespaceManager* ns_manager,
      SysClient* sys_client)
    : ns_manager_(ns_manager),
      sys_client_(sys_client) {}

NamespaceAwareExecutor::~NamespaceAwareExecutor() {}

void NamespaceAwareExecutor::SetEnvForChildProcess() {
  const char* path = NULL;

  DIR* d = opendir("/system/vendor/bin");
  if (d) {
    closedir(d);
    path = "/system/bin:/system/vendor/bin";
  } else {
    path = "/system/bin";
  }

  if (setenv("PATH", path, 1) == -1) {
    KLOG_WARNING(LOG_TAG, "Failed to set PATH.");
  }
}

bool NamespaceAwareExecutor::InternalNonInteractiveExecute(const char** commands) {
  SetEnvForChildProcess();

  for (size_t cmd_index = 0; commands[cmd_index]; ++cmd_index) {
    // Non-interactive: simply fire command and monitor output.
    KLOG_INFO(LOG_TAG, "# %s\n", commands[cmd_index]);
    UniquePtr<SysClient::ProcessPipe> pipe(
        sys_client_->POpen(commands[cmd_index]));
    const char* output = NULL;
    while ((output = pipe->GetOutputLine()) != NULL) {
      KLOG_INFO(LOG_TAG, "--- %s", output);
    }
    if (pipe->GetReturnCode() != 0) {
      KLOG_INFO(LOG_TAG, ">>> Command exited with return code %d.\n",
                pipe->GetReturnCode());
    }
  }
  return true;
}

bool NamespaceAwareExecutor::InternalInteractiveExecute(const char** commands) {
  SetEnvForChildProcess();

  for (size_t cmd_index = 0; commands[cmd_index]; ++cmd_index) {
    sys_client_->System(commands[cmd_index]);
  }
  return true;
}

int32_t NamespaceAwareExecutor::InternalExecute(
    const std::string& network_namespace,
    const ::avd::Callback<bool()>& callback) {
  if (!ns_manager_->SwitchNamespace(network_namespace)) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to set current namespace to %s.\n",
               __FUNCTION__, network_namespace.c_str());
    return 1;
  }

  callback();
  return 0;
}

bool NamespaceAwareExecutor::Execute(
    const std::string& namespace_name,
    bool is_interactive,
    const char** commands) {
  ::avd::Callback<int32_t()> callback(
      &NamespaceAwareExecutor::InternalExecute, this, namespace_name,
      ::avd::Callback<bool()>(
          is_interactive ?
              &NamespaceAwareExecutor::InternalInteractiveExecute :
              &NamespaceAwareExecutor::InternalNonInteractiveExecute,
          this, commands));
  UniquePtr<SysClient::ProcessHandle> handle(
      sys_client_->Clone(std::string("gce.ex.") + namespace_name,
                         callback, kCloneNewNS));

  return handle->WaitResult() == 0;
}

SysClient::ProcessHandle* NamespaceAwareExecutor::Execute(
    const std::string& namespace_name,
    ::avd::Callback<bool()> callback) {
  ::avd::Callback<int32_t()> internal_callback(
      &NamespaceAwareExecutor::InternalExecute, this, namespace_name, callback);

  return sys_client_->Clone(std::string("gce.ex.") + namespace_name,
                            internal_callback, kCloneNewNS);
}


NamespaceAwareExecutor* NamespaceAwareExecutor::New(
    NetworkNamespaceManager* ns_manager,
    SysClient* sys_client) {
  if (!ns_manager) return NULL;
  if (!sys_client) return NULL;
  return new NamespaceAwareExecutor(ns_manager, sys_client);
}

}  // namespace avd


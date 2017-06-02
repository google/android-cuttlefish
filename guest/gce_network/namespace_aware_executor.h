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
#ifndef GUEST_GCE_NETWORK_NAMESPACE_AWARE_EXECUTOR_H_
#define GUEST_GCE_NETWORK_NAMESPACE_AWARE_EXECUTOR_H_

#include <functional>
#include <string>

#include "guest/gce_network/network_namespace_manager.h"
#include "guest/gce_network/sys_client.h"

namespace avd {

// NamespaceAwareExecutor primary class.
// Manages network namespaces, interfaces and processes required for proper
// operation of AVD network.
class NamespaceAwareExecutor {
 public:
  ~NamespaceAwareExecutor();

  // Execute |commands| in |network_namespace|.
  // Creates dedicated process that will be relocated to target namespace.
  // Waits for process to complete.
  // If |is_interactive| is true, the new process will get access to user
  // console and will be able to interact.
  // Returns true on success.
  bool Execute(const std::string& network_namespace,
               bool is_interactive,
               const char** commands);

  // Execute |callback| in |network_namespace|.
  // Creates dedicated process that will be relocated to target namespace.
  // Returns handle to the process on success. Caller can either orphan the
  // pointer (to keep it running) or wait for its completion and fetch result.
  SysClient::ProcessHandle* Execute(
      const std::string& network_namespace,
      std::function<bool()> callback);

  // Validate parameters and create new instance of NamespaceAwareExecutor class.
  // Returns NULL if any of the supplied parameters is NULL.
  // Caller retains ownership of supplied arguments.
  static NamespaceAwareExecutor* New(
      NetworkNamespaceManager* ns_manager,
      SysClient* sys_client);

 private:
  NamespaceAwareExecutor(
      NetworkNamespaceManager* ns_manager,
      SysClient* sys_client);

  // Execute |callback| in |network_namespace|.
  // Returns 0 on success.
  // Called from separate process. Do not call directly
  int32_t InternalExecute(const std::string& network_namespace,
                          const std::function<bool()>& callback);

  bool InternalInteractiveExecute(const char** commands);
  bool InternalNonInteractiveExecute(const char** commands);

  // Set environment variables for the child processes only.
  void SetEnvForChildProcess();

  NetworkNamespaceManager* ns_manager_;
  SysClient* sys_client_;

  NamespaceAwareExecutor(const NamespaceAwareExecutor&);
  NamespaceAwareExecutor& operator= (const NamespaceAwareExecutor&);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_NAMESPACE_AWARE_EXECUTOR_H_

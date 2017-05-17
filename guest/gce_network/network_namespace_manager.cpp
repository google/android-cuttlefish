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
#include "api_level_fixes.h"
#include "network_namespace_manager.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/sockios.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <AutoResources.h>
#include <gce_fs.h>

#include "callback.h"
#include "jb_compat.h"

namespace avd {
namespace {
// Paranoid networking latch.
const int kSIOCSParanoid = 0x89df;
const char* kNamespaces[] = { "mnt", "net", "ipc", NULL };
const CloneFlags kNamespaceTypes =
    CloneFlags(kCloneNewNS | kCloneNewNet | kCloneNewIPC);

// Folder hosting network namespaces.
// In practice, this could be any folder we want it to be, but for the sake of
// ip netns command, we select the preferred location.
const char kNetNsFolder[] = "/var/run/netns";

// Implementation of Network Namespace manager.
class NetworkNamespaceManagerImpl : public NetworkNamespaceManager {
 public:
  NetworkNamespaceManagerImpl(SysClient* sys_client)
      : sys_client_(sys_client) {}

  virtual ~NetworkNamespaceManagerImpl() {}

  virtual int32_t GetNamespaceDescriptor(const std::string& ns_name);

  // Not exposed.
  bool CreateNamespaceRootFolder();

  // Create new network namespace.
  //
  // Creates new namespace fd in kNetNsFolder and binds it to current process'
  // network namespace descriptor.
  bool CreateNetworkNamespace(const std::string& ns_name, bool new_namespace,
                              bool is_paranoid);

  bool SwitchNamespace(const std::string& ns_name);

 private:
  std::string GetNamespacePath(
      const std::string& ns_name, const std::string& type);
  int NetworkNamespaceProcess(bool is_paranoid);

  SysClient* sys_client_;
};

std::string NetworkNamespaceManagerImpl::GetNamespacePath(
    const std::string& ns_name, const std::string& type) {
  size_t ns_len = ns_name.length();
  std::string new_name(ns_len, '\0');

  for (size_t index = 0; index < ns_len; ++index) {
    char c = ns_name[index];
    if (isalnum(c)) {
      new_name[index] = c;
    } else {
      new_name[index] = '_';
    }
  }

  std::string result(kNetNsFolder);
  result.append("/");
  result.append(new_name);
  result.append(".");
  result.append(type);

  return result;
}

bool NetworkNamespaceManagerImpl::CreateNamespaceRootFolder() {
  // Create root folder for network namespaces.
  if (gce_fs_mkdirs(
      kNetNsFolder, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)) {
    KLOG_ERROR(LOG_TAG, "%s: gs_fs_prepare_dir(%s) failed %s:%d (%s)\n",
               __FUNCTION__, kNetNsFolder, __FILE__, __LINE__, strerror(errno));
    return false;
  }

  return true;
}

int NetworkNamespaceManagerImpl::NetworkNamespaceProcess(bool is_paranoid) {
  // Replace current /sys fs with the one describing current network namespace.
  // This is required for namespace-oblivious tools (like dhcpcd) to work.
  if (sys_client_->Umount("/sys", MNT_DETACH) < 0) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to detach /sys: %s.\n",
               __FUNCTION__, strerror(errno));
    return 1;
  }

  if (sys_client_->Mount("none", "/sys", "sysfs", 0) < 0) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to re-attach /sys: %s.\n",
               __FUNCTION__, strerror(errno));
    return 1;
  }

  if (is_paranoid) {
    int netsocketfd = sys_client_->Socket(AF_INET, SOCK_DGRAM, 0);
    if (netsocketfd > 0) {
      if (sys_client_->IoCtl(netsocketfd, kSIOCSParanoid, NULL) < 0) {
        KLOG_ERROR(LOG_TAG, "%s: could not enable paranoid network: %d (%s)",
                   __FUNCTION__, errno, strerror(errno));
      }
    } else {
      KLOG_ERROR(LOG_TAG, "%s: could not create socket: %d (%s)", __FUNCTION__,
                 errno, strerror(errno));
    }
  }
  // Live forever.
  setsid();
  for (;;) pause();
  return -1;
}

bool NetworkNamespaceManagerImpl::CreateNetworkNamespace(
    const std::string& ns_name, bool new_namespace, bool is_paranoid) {
  // Leak handle on purpose. The process is expected to never finish, as it will
  // be the owner of the network namespace.
  SysClient::ProcessHandle* handle = sys_client_->Clone(
      std::string("gce.ns.") + ns_name,
      ::avd::Callback<int()>(&NetworkNamespaceManagerImpl::NetworkNamespaceProcess,
                      this, is_paranoid),
      new_namespace ? kNamespaceTypes : kCloneNewNS);

  // Bind the namespace so that processes can later switch between the
  // namespaces. Some processes (like remoter) may require this to change their
  // 'default' namespace to the desired one.
  AutoFreeBuffer proc_ns_file;
  AutoFreeBuffer glob_ns_file;

  for (size_t index = 0; kNamespaces[index] != NULL; ++index) {
    proc_ns_file.PrintF("/proc/%d/ns/%s",
                        handle->Pid(), kNamespaces[index]);
    glob_ns_file.PrintF("/var/run/netns/%s.%s",
                        ns_name.c_str(), kNamespaces[index]);
    symlink(proc_ns_file.data(), glob_ns_file.data());
  }

  KLOG_INFO(LOG_TAG, "Initialized network namespace %s\n", ns_name.c_str());

  // Some tools require a pid as opposed to fd to make changes to network
  // namespaces, such as reparenting.

  glob_ns_file.PrintF("/var/run/netns/%s.process", ns_name.c_str());
  int pid_fd = TEMP_FAILURE_RETRY(
      open(glob_ns_file.data(), O_RDWR | O_CREAT | O_EXCL, 0));
  if (pid_fd < 0) {
    KLOG_ERROR(LOG_TAG, "%s: open(%s) failed %s:%d (%s)\n",
               __FUNCTION__, glob_ns_file.data(), __FILE__, __LINE__,
               strerror(errno));
    return false;
  }

  proc_ns_file.PrintF("%d", handle->Pid());
  write(pid_fd, proc_ns_file.data(), proc_ns_file.size());
  close(pid_fd);

  return true;
}

int32_t NetworkNamespaceManagerImpl::GetNamespaceDescriptor(
    const std::string& ns_name) {
  std::string ns_path = GetNamespacePath(ns_name, "net");

  int netns = TEMP_FAILURE_RETRY(open(ns_path.c_str(), O_RDONLY));
  if (netns == -1) {
    KLOG_ERROR(LOG_TAG, "%s: Failed to open netns (%s) (%s).\n",
               __FUNCTION__, ns_name.c_str(), strerror(errno));
    return -1;
  }

  return netns;
}

bool NetworkNamespaceManagerImpl::SwitchNamespace(const std::string& ns_name) {
  // Abandon current namespace. If any process still uses it, they can continue
  // doing so as if nothing ever happened.
  sys_client_->Unshare(kNamespaceTypes);

  for (int type_index = 0; kNamespaces[type_index] != NULL; ++type_index) {
    std::string ns_path = GetNamespacePath(ns_name, kNamespaces[type_index]);

    int netns = TEMP_FAILURE_RETRY(open(ns_path.c_str(), O_RDONLY));
    if (netns == -1) {
      KLOG_ERROR(LOG_TAG, "%s: Failed to open netns (%s) (%s).\n",
                 __FUNCTION__, ns_name.c_str(), strerror(errno));
      return false;
    }

    if (sys_client_->SetNs(netns, 0) != 0) {
      KLOG_ERROR(LOG_TAG, "Could not change network namespace to %s: %s\n",
                 ns_name.c_str(), strerror(errno));
      return false;
    }
    close(netns);
  }

  return true;

}

}  // namespace

const char NetworkNamespaceManager::kAndroidNs[] = "android";
const char NetworkNamespaceManager::kOuterNs[] = "outer";

NetworkNamespaceManager* NetworkNamespaceManager::New(SysClient* sys_client) {
  if (sys_client == NULL) return NULL;

  NetworkNamespaceManagerImpl* ns_manager =
      new NetworkNamespaceManagerImpl(sys_client);

  if (!ns_manager->CreateNamespaceRootFolder()) {
    delete ns_manager;
    return NULL;
  }

  return ns_manager;
}

}  // namespace avd


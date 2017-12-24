/*
 * Copyright (C) 2017 The Android Open Source Project
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
#pragma once

#include <memory>
#include <string>

namespace config {
// GuestConfig builds XML document describing target VM.
// Documents built by GuestConfig can be directly used by libvirt to instantiate
// new virtual machine.
class GuestConfig {
 public:
  GuestConfig() = default;
  ~GuestConfig() = default;

  // Set instance ID.
  GuestConfig& SetID(int id) {
    id_ = id;
    return *this;
  }

  // Set number of virtual CPUs.
  GuestConfig& SetVCPUs(int vcpus) {
    vcpus_ = vcpus;
    return *this;
  }

  // Set total memory amount it MB.
  GuestConfig& SetMemoryMB(int mem_mb) {
    memory_mb_ = mem_mb;
    return *this;
  }

  // Set kernel path.
  GuestConfig& SetKernelName(const std::string& kernel) {
    kernel_name_ = kernel;
    return *this;
  }

  // Set kernel cmdline arguments.
  GuestConfig& SetKernelArgs(const std::string& args) {
    kernel_args_ = args;
    return *this;
  }

  // Set initrd path.
  GuestConfig& SetInitRDName(const std::string& initrd) {
    initrd_name_ = initrd;
    return *this;
  }

  // Set Android system partition image path.
  GuestConfig& SetSystemPartitionPath(const std::string& path) {
    system_partition_path_ = path;
    return *this;
  }

  // Set Android data partition image path.
  GuestConfig& SetCachePartitionPath(const std::string& path) {
    cache_partition_path_ = path;
    return *this;
  }

  // Set Android data partition image path.
  GuestConfig& SetDataPartitionPath(const std::string& path) {
    data_partition_path_ = path;
    return *this;
  }

  // Set Android vendor partition image path.
  GuestConfig& SetVendorPartitionPath(const std::string& path) {
    vendor_partition_path_ = path;
    return *this;
  }

  // Set ivshmem server socket path.
  GuestConfig& SetIVShMemSocketPath(const std::string& path) {
    ivshmem_socket_path_ = path;
    return *this;
  }

  // Set number of vectors supplied by ivserver.
  GuestConfig& SetIVShMemVectorCount(int count) {
    ivshmem_vector_count_ = count;
    return *this;
  }

  // Set name of the mobile bridge, eg. br0
  GuestConfig& SetMobileBridgeName(const std::string& name) {
    mobile_bridge_name_ = name;
    return *this;
  }

  // Set source of entropy, eg. /dev/urandom.
  GuestConfig& SetEntropySource(const std::string& source) {
    entropy_source_ = source;
    return *this;
  }

  // Flags to disable the AppArmor security features of libvirt
  GuestConfig& SetDisableAppArmorSecurity(bool value) {
    disable_app_armor_security_ = value;
    return *this;
  }

  // Flags to disable the DAC security features of libvirt
  GuestConfig& SetDisableDACSecurity(bool value) {
    disable_dac_security_ = value;
    return *this;
  }

  // The UUID that libvirt uses to identify the instance
  GuestConfig& SetUUID(const std::string& uuid) {
    uuid_ = uuid;
    return *this;
  }

  // GetInstanceName returns name of this newly created instance.
  std::string GetInstanceName() const;

  // GetUSBSocketName returns name of the USB socket that will be used to
  // forward access to USB gadget. This is for V1 of the USB bus.
  std::string GetUSBV1SocketName() const { return usb_v1_socket_name_; }

  GuestConfig& SetUSBV1SocketName(const std::string& source) {
    usb_v1_socket_name_ = source;
    return *this;
  }

  std::string GetKernelLogSocketName() const { return kernel_log_socket_name_; }

  GuestConfig& SetKernelLogSocketName(const std::string& source) {
    kernel_log_socket_name_ = source;
    return *this;
  }

  // Build document as formatted XML string.
  std::string Build() const;

 private:
  int id_;
  int vcpus_;
  int memory_mb_;

  std::string kernel_name_;
  std::string kernel_args_;
  std::string initrd_name_;

  std::string system_partition_path_;
  std::string cache_partition_path_;
  std::string data_partition_path_;
  std::string vendor_partition_path_;
  std::string usb_v1_socket_name_;
  std::string kernel_log_socket_name_;

  std::string ivshmem_socket_path_;
  int ivshmem_vector_count_;

  std::string mobile_bridge_name_;
  std::string entropy_source_;

  std::string uuid_;
  bool disable_dac_security_;
  bool disable_app_armor_security_;

  GuestConfig(const GuestConfig&) = delete;
  GuestConfig& operator=(const GuestConfig&) = delete;
};

}  // namespace config

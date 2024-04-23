/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish_config.h"
#include "host/libs/config/cuttlefish_config.h"

#include <string_view>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flags_validator.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/gem5_manager.h"

namespace cuttlefish {
namespace {

using APBootFlow = CuttlefishConfig::InstanceSpecific::APBootFlow;

const char* kInstances = "instances";

std::string IdToName(const std::string& id) { return kCvdNamePrefix + id; }

}  // namespace

std::ostream& operator<<(std::ostream& out, ExternalNetworkMode net) {
  switch (net) {
    case ExternalNetworkMode::kUnknown:
      return out << "unknown";
    case ExternalNetworkMode::kTap:
      return out << "tap";
    case ExternalNetworkMode::kSlirp:
      return out << "slirp";
  }
}
Result<ExternalNetworkMode> ParseExternalNetworkMode(std::string_view str) {
  if (android::base::EqualsIgnoreCase(str, "tap")) {
    return ExternalNetworkMode::kTap;
  } else if (android::base::EqualsIgnoreCase(str, "slirp")) {
    return ExternalNetworkMode::kSlirp;
  } else {
    return CF_ERRF(
        "\"{}\" is not a valid ExternalNetworkMode. Valid values are \"tap\" "
        "and \"slirp\"",
        str);
  }
}

static constexpr char kInstanceDir[] = "instance_dir";
CuttlefishConfig::MutableInstanceSpecific::MutableInstanceSpecific(
    CuttlefishConfig* config, const std::string& id)
    : config_(config), id_(id) {
  // Legacy for acloud
  (*Dictionary())[kInstanceDir] = config_->InstancesPath(IdToName(id));
}

Json::Value* CuttlefishConfig::MutableInstanceSpecific::Dictionary() {
  return &(*config_->dictionary_)[kInstances][id_];
}

const Json::Value* CuttlefishConfig::InstanceSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kInstances][id_];
}

std::string CuttlefishConfig::InstanceSpecific::instance_dir() const {
  return config_->InstancesPath(IdToName(id_));
}

std::string CuttlefishConfig::InstanceSpecific::instance_internal_dir() const {
  return PerInstancePath(kInternalDirName);
}

std::string CuttlefishConfig::InstanceSpecific::instance_uds_dir() const {
  return config_->InstancesUdsPath(IdToName(id_));
}

std::string CuttlefishConfig::InstanceSpecific::instance_internal_uds_dir()
    const {
  return PerInstanceUdsPath(kInternalDirName);
}

// TODO (b/163575714) add virtio console support to the bootloader so the
// virtio console path for the console device can be taken again. When that
// happens, this function can be deleted along with all the code paths it
// forces.
bool CuttlefishConfig::InstanceSpecific::use_bootloader() const {
  return true;
};

// vectorized and moved system image files into instance specific
static constexpr char kBootImage[] = "boot_image";
std::string CuttlefishConfig::InstanceSpecific::boot_image() const {
  return (*Dictionary())[kBootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_boot_image(
    const std::string& boot_image) {
  (*Dictionary())[kBootImage] = boot_image;
}
static constexpr char kNewBootImage[] = "new_boot_image";
std::string CuttlefishConfig::InstanceSpecific::new_boot_image() const {
  return (*Dictionary())[kNewBootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_new_boot_image(
    const std::string& new_boot_image) {
  (*Dictionary())[kNewBootImage] = new_boot_image;
}
static constexpr char kInitBootImage[] = "init_boot_image";
std::string CuttlefishConfig::InstanceSpecific::init_boot_image() const {
  return (*Dictionary())[kInitBootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_init_boot_image(
    const std::string& init_boot_image) {
  (*Dictionary())[kInitBootImage] = init_boot_image;
}
static constexpr char kDataImage[] = "data_image";
std::string CuttlefishConfig::InstanceSpecific::data_image() const {
  return (*Dictionary())[kDataImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_data_image(
    const std::string& data_image) {
  (*Dictionary())[kDataImage] = data_image;
}
static constexpr char kNewDataImage[] = "new_data_image";
std::string CuttlefishConfig::InstanceSpecific::new_data_image() const {
  return (*Dictionary())[kNewDataImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_new_data_image(
    const std::string& new_data_image) {
  (*Dictionary())[kNewDataImage] = new_data_image;
}
static constexpr char kSuperImage[] = "super_image";
std::string CuttlefishConfig::InstanceSpecific::super_image() const {
  return (*Dictionary())[kSuperImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_super_image(
    const std::string& super_image) {
  (*Dictionary())[kSuperImage] = super_image;
}
static constexpr char kNewSuperImage[] = "new_super_image";
std::string CuttlefishConfig::InstanceSpecific::new_super_image() const {
  return (*Dictionary())[kNewSuperImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_new_super_image(
    const std::string& super_image) {
  (*Dictionary())[kNewSuperImage] = super_image;
}
static constexpr char kMiscInfoTxt[] = "misc_info_txt";
std::string CuttlefishConfig::InstanceSpecific::misc_info_txt() const {
  return (*Dictionary())[kMiscInfoTxt].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_misc_info_txt(
    const std::string& misc_info) {
  (*Dictionary())[kMiscInfoTxt] = misc_info;
}
static constexpr char kVendorBootImage[] = "vendor_boot_image";
std::string CuttlefishConfig::InstanceSpecific::vendor_boot_image() const {
  return (*Dictionary())[kVendorBootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vendor_boot_image(
    const std::string& vendor_boot_image) {
  (*Dictionary())[kVendorBootImage] = vendor_boot_image;
}
static constexpr char kNewVendorBootImage[] = "new_vendor_boot_image";
std::string CuttlefishConfig::InstanceSpecific::new_vendor_boot_image() const {
  return (*Dictionary())[kNewVendorBootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_new_vendor_boot_image(
    const std::string& new_vendor_boot_image) {
  (*Dictionary())[kNewVendorBootImage] = new_vendor_boot_image;
}
static constexpr char kVbmetaImage[] = "vbmeta_image";
std::string CuttlefishConfig::InstanceSpecific::vbmeta_image() const {
  return (*Dictionary())[kVbmetaImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vbmeta_image(
    const std::string& vbmeta_image) {
  (*Dictionary())[kVbmetaImage] = vbmeta_image;
}
static constexpr char kNewVbmetaImage[] = "new_vbmeta_image";
std::string CuttlefishConfig::InstanceSpecific::new_vbmeta_image() const {
  return (*Dictionary())[kNewVbmetaImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_new_vbmeta_image(
    const std::string& new_vbmeta_image) {
  (*Dictionary())[kNewVbmetaImage] = new_vbmeta_image;
}
static constexpr char kVbmetaSystemImage[] = "vbmeta_system_image";
std::string CuttlefishConfig::InstanceSpecific::vbmeta_system_image() const {
  return (*Dictionary())[kVbmetaSystemImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vbmeta_system_image(
    const std::string& vbmeta_system_image) {
  (*Dictionary())[kVbmetaSystemImage] = vbmeta_system_image;
}
static constexpr char kVbmetaVendorDlkmImage[] = "vbmeta_vendor_dlkm_image";
std::string CuttlefishConfig::InstanceSpecific::vbmeta_vendor_dlkm_image()
    const {
  return (*Dictionary())[kVbmetaVendorDlkmImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vbmeta_vendor_dlkm_image(
    const std::string& image) {
  (*Dictionary())[kVbmetaVendorDlkmImage] = image;
}
static constexpr char kNewVbmetaVendorDlkmImage[] =
    "new_vbmeta_vendor_dlkm_image";
std::string CuttlefishConfig::InstanceSpecific::new_vbmeta_vendor_dlkm_image()
    const {
  return (*Dictionary())[kNewVbmetaVendorDlkmImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::
    set_new_vbmeta_vendor_dlkm_image(const std::string& image) {
  (*Dictionary())[kNewVbmetaVendorDlkmImage] = image;
}
static constexpr char kVbmetaSystemDlkmImage[] = "vbmeta_system_dlkm_image";
std::string CuttlefishConfig::InstanceSpecific::vbmeta_system_dlkm_image()
    const {
  return (*Dictionary())[kVbmetaSystemDlkmImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vbmeta_system_dlkm_image(
    const std::string& image) {
  (*Dictionary())[kVbmetaSystemDlkmImage] = image;
}
static constexpr char kNewVbmetaSystemDlkmImage[] =
    "new_vbmeta_system_dlkm_image";
std::string CuttlefishConfig::InstanceSpecific::new_vbmeta_system_dlkm_image()
    const {
  return (*Dictionary())[kNewVbmetaSystemDlkmImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::
    set_new_vbmeta_system_dlkm_image(const std::string& image) {
  (*Dictionary())[kNewVbmetaSystemDlkmImage] = image;
}
static constexpr char kOtherosEspImage[] = "otheros_esp_image";
std::string CuttlefishConfig::InstanceSpecific::otheros_esp_image() const {
  return (*Dictionary())[kOtherosEspImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_otheros_esp_image(
    const std::string& otheros_esp_image) {
  (*Dictionary())[kOtherosEspImage] = otheros_esp_image;
}
static constexpr char kAndroidEfiLoader[] = "android_efi_loader";
std::string CuttlefishConfig::InstanceSpecific::android_efi_loader() const {
  return (*Dictionary())[kAndroidEfiLoader].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_android_efi_loader(
    const std::string& android_efi_loader) {
  (*Dictionary())[kAndroidEfiLoader] = android_efi_loader;
}
static constexpr char kChromeOsDisk[] = "chromeos_disk";
std::string CuttlefishConfig::InstanceSpecific::chromeos_disk() const {
  return (*Dictionary())[kChromeOsDisk].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_chromeos_disk(
    const std::string& chromeos_disk) {
  (*Dictionary())[kChromeOsDisk] = chromeos_disk;
}
static constexpr char kChromeOsKernelPath[] = "chromeos_kernel_path";
std::string CuttlefishConfig::InstanceSpecific::chromeos_kernel_path() const {
  return (*Dictionary())[kChromeOsKernelPath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_chromeos_kernel_path(
    const std::string& chromeos_kernel_path) {
  (*Dictionary())[kChromeOsKernelPath] = chromeos_kernel_path;
}
static constexpr char kChromeOsRootImage[] = "chromeos_root_image";
std::string CuttlefishConfig::InstanceSpecific::chromeos_root_image() const {
  return (*Dictionary())[kChromeOsRootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_chromeos_root_image(
    const std::string& chromeos_root_image) {
  (*Dictionary())[kChromeOsRootImage] = chromeos_root_image;
}
static constexpr char kLinuxKernelPath[] = "linux_kernel_path";
std::string CuttlefishConfig::InstanceSpecific::linux_kernel_path() const {
  return (*Dictionary())[kLinuxKernelPath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_linux_kernel_path(
    const std::string& linux_kernel_path) {
  (*Dictionary())[kLinuxKernelPath] = linux_kernel_path;
}
static constexpr char kLinuxInitramfsPath[] = "linux_initramfs_path";
std::string CuttlefishConfig::InstanceSpecific::linux_initramfs_path() const {
  return (*Dictionary())[kLinuxInitramfsPath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_linux_initramfs_path(
    const std::string& linux_initramfs_path) {
  (*Dictionary())[kLinuxInitramfsPath] = linux_initramfs_path;
}
static constexpr char kLinuxRootImage[] = "linux_root_image";
std::string CuttlefishConfig::InstanceSpecific::linux_root_image() const {
  return (*Dictionary())[kLinuxRootImage].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_linux_root_image(
    const std::string& linux_root_image) {
  (*Dictionary())[kLinuxRootImage] = linux_root_image;
}
static constexpr char kFuchsiaZedbootPath[] = "fuchsia_zedboot_path";
void CuttlefishConfig::MutableInstanceSpecific::set_fuchsia_zedboot_path(
    const std::string& fuchsia_zedboot_path) {
  (*Dictionary())[kFuchsiaZedbootPath] = fuchsia_zedboot_path;
}
std::string CuttlefishConfig::InstanceSpecific::fuchsia_zedboot_path() const {
  return (*Dictionary())[kFuchsiaZedbootPath].asString();
}
static constexpr char kFuchsiaMultibootBinPath[] = "multiboot_bin_path";
void CuttlefishConfig::MutableInstanceSpecific::set_fuchsia_multiboot_bin_path(
    const std::string& fuchsia_multiboot_bin_path) {
  (*Dictionary())[kFuchsiaMultibootBinPath] = fuchsia_multiboot_bin_path;
}
std::string CuttlefishConfig::InstanceSpecific::fuchsia_multiboot_bin_path() const {
  return (*Dictionary())[kFuchsiaMultibootBinPath].asString();
}
static constexpr char kFuchsiaRootImage[] = "fuchsia_root_image";
void CuttlefishConfig::MutableInstanceSpecific::set_fuchsia_root_image(
    const std::string& fuchsia_root_image) {
  (*Dictionary())[kFuchsiaRootImage] = fuchsia_root_image;
}
std::string CuttlefishConfig::InstanceSpecific::fuchsia_root_image() const {
  return (*Dictionary())[kFuchsiaRootImage].asString();
}
static constexpr char kCustomPartitionPath[] = "custom_partition_path";
void CuttlefishConfig::MutableInstanceSpecific::set_custom_partition_path(
    const std::string& custom_partition_path) {
  (*Dictionary())[kCustomPartitionPath] = custom_partition_path;
}
std::string CuttlefishConfig::InstanceSpecific::custom_partition_path() const {
  return (*Dictionary())[kCustomPartitionPath].asString();
}
static constexpr char kBlankMetadataImageMb[] = "blank_metadata_image_mb";
int CuttlefishConfig::InstanceSpecific::blank_metadata_image_mb() const {
  return (*Dictionary())[kBlankMetadataImageMb].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_blank_metadata_image_mb(
    int blank_metadata_image_mb) {
  (*Dictionary())[kBlankMetadataImageMb] = blank_metadata_image_mb;
}
static constexpr char kBlankSdcardImageMb[] = "blank_sdcard_image_mb";
int CuttlefishConfig::InstanceSpecific::blank_sdcard_image_mb() const {
  return (*Dictionary())[kBlankSdcardImageMb].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_blank_sdcard_image_mb(
    int blank_sdcard_image_mb) {
  (*Dictionary())[kBlankSdcardImageMb] = blank_sdcard_image_mb;
}
static constexpr char kBootloader[] = "bootloader";
std::string CuttlefishConfig::InstanceSpecific::bootloader() const {
  return (*Dictionary())[kBootloader].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_bootloader(
    const std::string& bootloader) {
  (*Dictionary())[kBootloader] = bootloader;
}
static constexpr char kInitramfsPath[] = "initramfs_path";
std::string CuttlefishConfig::InstanceSpecific::initramfs_path() const {
  return (*Dictionary())[kInitramfsPath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_initramfs_path(
    const std::string& initramfs_path) {
  (*Dictionary())[kInitramfsPath] = initramfs_path;
}
static constexpr char kKernelPath[] = "kernel_path";
std::string CuttlefishConfig::InstanceSpecific::kernel_path() const {
  return (*Dictionary())[kKernelPath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_kernel_path(
    const std::string& kernel_path) {
  (*Dictionary())[kKernelPath] = kernel_path;
}
// end of system image files

static constexpr char kDefaultTargetZip[] = "default_target_zip";
std::string CuttlefishConfig::InstanceSpecific::default_target_zip() const {
  return (*Dictionary())[kDefaultTargetZip].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_default_target_zip(
    const std::string& default_target_zip) {
  (*Dictionary())[kDefaultTargetZip] = default_target_zip;
}
static constexpr char kSystemTargetZip[] = "system_target_zip";
std::string CuttlefishConfig::InstanceSpecific::system_target_zip() const {
  return (*Dictionary())[kSystemTargetZip].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_system_target_zip(
    const std::string& system_target_zip) {
  (*Dictionary())[kSystemTargetZip] = system_target_zip;
}

static constexpr char kSerialNumber[] = "serial_number";
std::string CuttlefishConfig::InstanceSpecific::serial_number() const {
  return (*Dictionary())[kSerialNumber].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_serial_number(
    const std::string& serial_number) {
  (*Dictionary())[kSerialNumber] = serial_number;
}

static constexpr char kVirtualDiskPaths[] = "virtual_disk_paths";
std::vector<std::string> CuttlefishConfig::InstanceSpecific::virtual_disk_paths() const {
  std::vector<std::string> virtual_disks;
  auto virtual_disks_json_obj = (*Dictionary())[kVirtualDiskPaths];
  for (const auto& disk : virtual_disks_json_obj) {
    virtual_disks.push_back(disk.asString());
  }
  return virtual_disks;
}
void CuttlefishConfig::MutableInstanceSpecific::set_virtual_disk_paths(
    const std::vector<std::string>& virtual_disk_paths) {
  Json::Value virtual_disks_json_obj(Json::arrayValue);
  for (const auto& arg : virtual_disk_paths) {
    virtual_disks_json_obj.append(arg);
  }
  (*Dictionary())[kVirtualDiskPaths] = virtual_disks_json_obj;
}

static constexpr char kGuestAndroidVersion[] = "guest_android_version";
std::string CuttlefishConfig::InstanceSpecific::guest_android_version() const {
  return (*Dictionary())[kGuestAndroidVersion].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_guest_android_version(
    const std::string& guest_android_version) {
  (*Dictionary())[kGuestAndroidVersion] = guest_android_version;
}

static constexpr char kBootconfigSupported[] = "bootconfig_supported";
bool CuttlefishConfig::InstanceSpecific::bootconfig_supported() const {
  return (*Dictionary())[kBootconfigSupported].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_bootconfig_supported(
    bool bootconfig_supported) {
  (*Dictionary())[kBootconfigSupported] = bootconfig_supported;
}

static constexpr char kFilenameEncryptionMode[] = "filename_encryption_mode";
std::string CuttlefishConfig::InstanceSpecific::filename_encryption_mode() const {
  return (*Dictionary())[kFilenameEncryptionMode].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_filename_encryption_mode(
    const std::string& filename_encryption_mode) {
  auto fmt = filename_encryption_mode;
  std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);
  (*Dictionary())[kFilenameEncryptionMode] = fmt;
}

static constexpr char kExternalNetworkMode[] = "external_network_mode";
ExternalNetworkMode CuttlefishConfig::InstanceSpecific::external_network_mode()
    const {
  auto str = (*Dictionary())[kExternalNetworkMode].asString();
  return ParseExternalNetworkMode(str).value_or(ExternalNetworkMode::kUnknown);
}
void CuttlefishConfig::MutableInstanceSpecific::set_external_network_mode(
    ExternalNetworkMode mode) {
  (*Dictionary())[kExternalNetworkMode] = fmt::format("{}", mode);
}

std::string CuttlefishConfig::InstanceSpecific::kernel_log_pipe_name() const {
  return AbsolutePath(PerInstanceInternalPath("kernel-log-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::console_pipe_prefix() const {
  return AbsolutePath(PerInstanceInternalPath("console"));
}

std::string CuttlefishConfig::InstanceSpecific::console_in_pipe_name() const {
  return console_pipe_prefix() + ".in";
}

std::string CuttlefishConfig::InstanceSpecific::console_out_pipe_name() const {
  return console_pipe_prefix() + ".out";
}

std::string CuttlefishConfig::InstanceSpecific::gnss_pipe_prefix() const {
  return AbsolutePath(PerInstanceInternalPath("gnss"));
}

std::string CuttlefishConfig::InstanceSpecific::gnss_in_pipe_name() const {
  return gnss_pipe_prefix() + ".in";
}

std::string CuttlefishConfig::InstanceSpecific::gnss_out_pipe_name() const {
  return gnss_pipe_prefix() + ".out";
}

static constexpr char kGnssGrpcProxyServerPort[] =
    "gnss_grpc_proxy_server_port";
int CuttlefishConfig::InstanceSpecific::gnss_grpc_proxy_server_port() const {
  return (*Dictionary())[kGnssGrpcProxyServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gnss_grpc_proxy_server_port(
    int gnss_grpc_proxy_server_port) {
  (*Dictionary())[kGnssGrpcProxyServerPort] = gnss_grpc_proxy_server_port;
}

static constexpr char kGnssFilePath[] = "gnss_file_path";
std::string CuttlefishConfig::InstanceSpecific::gnss_file_path() const {
  return (*Dictionary())[kGnssFilePath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gnss_file_path(
  const std::string& gnss_file_path) {
  (*Dictionary())[kGnssFilePath] = gnss_file_path;
}

static constexpr char kFixedLocationFilePath[] = "fixed_location_file_path";
std::string CuttlefishConfig::InstanceSpecific::fixed_location_file_path()
    const {
  return (*Dictionary())[kFixedLocationFilePath].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_fixed_location_file_path(
    const std::string& fixed_location_file_path) {
  (*Dictionary())[kFixedLocationFilePath] = fixed_location_file_path;
}

static constexpr char kGem5BinaryDir[] = "gem5_binary_dir";
std::string CuttlefishConfig::InstanceSpecific::gem5_binary_dir() const {
  return (*Dictionary())[kGem5BinaryDir].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gem5_binary_dir(
    const std::string& gem5_binary_dir) {
  (*Dictionary())[kGem5BinaryDir] = gem5_binary_dir;
}

static constexpr char kGem5CheckpointDir[] = "gem5_checkpoint_dir";
std::string CuttlefishConfig::InstanceSpecific::gem5_checkpoint_dir() const {
  return (*Dictionary())[kGem5CheckpointDir].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gem5_checkpoint_dir(
    const std::string& gem5_checkpoint_dir) {
  (*Dictionary())[kGem5CheckpointDir] = gem5_checkpoint_dir;
}

static constexpr char kKgdb[] = "kgdb";
void CuttlefishConfig::MutableInstanceSpecific::set_kgdb(bool kgdb) {
  (*Dictionary())[kKgdb] = kgdb;
}
bool CuttlefishConfig::InstanceSpecific::kgdb() const {
  return (*Dictionary())[kKgdb].asBool();
}

static constexpr char kCpus[] = "cpus";
void CuttlefishConfig::MutableInstanceSpecific::set_cpus(int cpus) { (*Dictionary())[kCpus] = cpus; }
int CuttlefishConfig::InstanceSpecific::cpus() const { return (*Dictionary())[kCpus].asInt(); }

static constexpr char kDataPolicy[] = "data_policy";
void CuttlefishConfig::MutableInstanceSpecific::set_data_policy(
    const std::string& data_policy) {
  (*Dictionary())[kDataPolicy] = data_policy;
}
std::string CuttlefishConfig::InstanceSpecific::data_policy() const {
  return (*Dictionary())[kDataPolicy].asString();
}

static constexpr char kBlankDataImageMb[] = "blank_data_image_mb";
void CuttlefishConfig::MutableInstanceSpecific::set_blank_data_image_mb(
    int blank_data_image_mb) {
  (*Dictionary())[kBlankDataImageMb] = blank_data_image_mb;
}
int CuttlefishConfig::InstanceSpecific::blank_data_image_mb() const {
  return (*Dictionary())[kBlankDataImageMb].asInt();
}

static constexpr char kGdbPort[] = "gdb_port";
void CuttlefishConfig::MutableInstanceSpecific::set_gdb_port(int port) {
  (*Dictionary())[kGdbPort] = port;
}
int CuttlefishConfig::InstanceSpecific::gdb_port() const {
  return (*Dictionary())[kGdbPort].asInt();
}

static constexpr char kMemoryMb[] = "memory_mb";
int CuttlefishConfig::InstanceSpecific::memory_mb() const {
  return (*Dictionary())[kMemoryMb].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_memory_mb(int memory_mb) {
  (*Dictionary())[kMemoryMb] = memory_mb;
}

static constexpr char kDdrMemMb[] = "ddr_mem_mb";
int CuttlefishConfig::InstanceSpecific::ddr_mem_mb() const {
  return (*Dictionary())[kDdrMemMb].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_ddr_mem_mb(int ddr_mem_mb) {
  (*Dictionary())[kDdrMemMb] = ddr_mem_mb;
}

static constexpr char kSetupWizardMode[] = "setupwizard_mode";
std::string CuttlefishConfig::InstanceSpecific::setupwizard_mode() const {
  return (*Dictionary())[kSetupWizardMode].asString();
}
Result<void> CuttlefishConfig::MutableInstanceSpecific::set_setupwizard_mode(
    const std::string& mode) {
  CF_EXPECT(ValidateSetupWizardMode(mode),
            "setupwizard_mode flag has invalid value: " << mode);
  (*Dictionary())[kSetupWizardMode] = mode;
  return {};
}

static constexpr char kUserdataFormat[] = "userdata_format";
std::string CuttlefishConfig::InstanceSpecific::userdata_format() const {
  return (*Dictionary())[kUserdataFormat].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_userdata_format(const std::string& userdata_format) {
  auto fmt = userdata_format;
  std::transform(fmt.begin(), fmt.end(), fmt.begin(), ::tolower);
  (*Dictionary())[kUserdataFormat] = fmt;
}

static constexpr char kGuestEnforceSecurity[] = "guest_enforce_security";
void CuttlefishConfig::MutableInstanceSpecific::set_guest_enforce_security(bool guest_enforce_security) {
  (*Dictionary())[kGuestEnforceSecurity] = guest_enforce_security;
}
bool CuttlefishConfig::InstanceSpecific::guest_enforce_security() const {
  return (*Dictionary())[kGuestEnforceSecurity].asBool();
}

static constexpr char kUseSdcard[] = "use_sdcard";
void CuttlefishConfig::MutableInstanceSpecific::set_use_sdcard(bool use_sdcard) {
  (*Dictionary())[kUseSdcard] = use_sdcard;
}
bool CuttlefishConfig::InstanceSpecific::use_sdcard() const {
  return (*Dictionary())[kUseSdcard].asBool();
}

static constexpr char kPauseInBootloader[] = "pause_in_bootloader";
void CuttlefishConfig::MutableInstanceSpecific::set_pause_in_bootloader(bool pause_in_bootloader) {
  (*Dictionary())[kPauseInBootloader] = pause_in_bootloader;
}
bool CuttlefishConfig::InstanceSpecific::pause_in_bootloader() const {
  return (*Dictionary())[kPauseInBootloader].asBool();
}

static constexpr char kRunAsDaemon[] = "run_as_daemon";
bool CuttlefishConfig::InstanceSpecific::run_as_daemon() const {
  return (*Dictionary())[kRunAsDaemon].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_run_as_daemon(bool run_as_daemon) {
  (*Dictionary())[kRunAsDaemon] = run_as_daemon;
}

static constexpr char kEnableMinimalMode[] = "enable_minimal_mode";
bool CuttlefishConfig::InstanceSpecific::enable_minimal_mode() const {
  return (*Dictionary())[kEnableMinimalMode].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_enable_minimal_mode(
    bool enable_minimal_mode) {
  (*Dictionary())[kEnableMinimalMode] = enable_minimal_mode;
}

static constexpr char kRunModemSimulator[] = "enable_modem_simulator";
bool CuttlefishConfig::InstanceSpecific::enable_modem_simulator() const {
  return (*Dictionary())[kRunModemSimulator].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_enable_modem_simulator(
    bool enable_modem_simulator) {
  (*Dictionary())[kRunModemSimulator] = enable_modem_simulator;
}

static constexpr char kModemSimulatorInstanceNumber[] =
    "modem_simulator_instance_number";
void CuttlefishConfig::MutableInstanceSpecific::
    set_modem_simulator_instance_number(int instance_number) {
  (*Dictionary())[kModemSimulatorInstanceNumber] = instance_number;
}
int CuttlefishConfig::InstanceSpecific::modem_simulator_instance_number()
    const {
  return (*Dictionary())[kModemSimulatorInstanceNumber].asInt();
}

static constexpr char kModemSimulatorSimType[] = "modem_simulator_sim_type";
void CuttlefishConfig::MutableInstanceSpecific::set_modem_simulator_sim_type(
    int sim_type) {
  (*Dictionary())[kModemSimulatorSimType] = sim_type;
}
int CuttlefishConfig::InstanceSpecific::modem_simulator_sim_type() const {
  return (*Dictionary())[kModemSimulatorSimType].asInt();
}

static constexpr char kGpuMode[] = "gpu_mode";
std::string CuttlefishConfig::InstanceSpecific::gpu_mode() const {
  return (*Dictionary())[kGpuMode].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gpu_mode(const std::string& name) {
  (*Dictionary())[kGpuMode] = name;
}

static constexpr char kGpuAngleFeatureOverridesEnabled[] =
    "gpu_angle_feature_overrides_enabled";
std::string
CuttlefishConfig::InstanceSpecific::gpu_angle_feature_overrides_enabled()
    const {
  return (*Dictionary())[kGpuAngleFeatureOverridesEnabled].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::
    set_gpu_angle_feature_overrides_enabled(const std::string& overrides) {
  (*Dictionary())[kGpuAngleFeatureOverridesEnabled] = overrides;
}

static constexpr char kGpuAngleFeatureOverridesDisabled[] =
    "gpu_angle_feature_overrides_disabled";
std::string
CuttlefishConfig::InstanceSpecific::gpu_angle_feature_overrides_disabled()
    const {
  return (*Dictionary())[kGpuAngleFeatureOverridesDisabled].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::
    set_gpu_angle_feature_overrides_disabled(const std::string& overrides) {
  (*Dictionary())[kGpuAngleFeatureOverridesDisabled] = overrides;
}

static constexpr char kGpuCaptureBinary[] = "gpu_capture_binary";
std::string CuttlefishConfig::InstanceSpecific::gpu_capture_binary() const {
  return (*Dictionary())[kGpuCaptureBinary].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gpu_capture_binary(const std::string& name) {
  (*Dictionary())[kGpuCaptureBinary] = name;
}

static constexpr char kGpuGfxstreamTransport[] = "gpu_gfxstream_transport";
std::string CuttlefishConfig::InstanceSpecific::gpu_gfxstream_transport()
    const {
  return (*Dictionary())[kGpuGfxstreamTransport].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gpu_gfxstream_transport(
    const std::string& transport) {
  (*Dictionary())[kGpuGfxstreamTransport] = transport;
}

static constexpr char kGpuRendererFeatures[] = "gpu_renderer_features";
std::string CuttlefishConfig::InstanceSpecific::gpu_renderer_features() const {
  return (*Dictionary())[kGpuRendererFeatures].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gpu_renderer_features(
    const std::string& transport) {
  (*Dictionary())[kGpuRendererFeatures] = transport;
}

static constexpr char kGpuContextTypes[] = "gpu_context_types";
std::string CuttlefishConfig::InstanceSpecific::gpu_context_types() const {
  return (*Dictionary())[kGpuContextTypes].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gpu_context_types(
    const std::string& context_types) {
  (*Dictionary())[kGpuContextTypes] = context_types;
}

static constexpr char kVulkanDriver[] = "guest_vulkan_driver";
std::string CuttlefishConfig::InstanceSpecific::guest_vulkan_driver() const {
  return (*Dictionary())[kVulkanDriver].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_guest_vulkan_driver(
    const std::string& driver) {
  (*Dictionary())[kVulkanDriver] = driver;
}

static constexpr char kRestartSubprocesses[] = "restart_subprocesses";
bool CuttlefishConfig::InstanceSpecific::restart_subprocesses() const {
  return (*Dictionary())[kRestartSubprocesses].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_restart_subprocesses(bool restart_subprocesses) {
  (*Dictionary())[kRestartSubprocesses] = restart_subprocesses;
}

static constexpr char kHWComposer[] = "hwcomposer";
std::string CuttlefishConfig::InstanceSpecific::hwcomposer() const {
  return (*Dictionary())[kHWComposer].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_hwcomposer(const std::string& name) {
  (*Dictionary())[kHWComposer] = name;
}

static constexpr char kEnableGpuUdmabuf[] = "enable_gpu_udmabuf";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_gpu_udmabuf(const bool enable_gpu_udmabuf) {
  (*Dictionary())[kEnableGpuUdmabuf] = enable_gpu_udmabuf;
}
bool CuttlefishConfig::InstanceSpecific::enable_gpu_udmabuf() const {
  return (*Dictionary())[kEnableGpuUdmabuf].asBool();
}

static constexpr char kEnableGpuVhostUser[] = "enable_gpu_vhost_user";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_gpu_vhost_user(
    const bool enable_gpu_vhost_user) {
  (*Dictionary())[kEnableGpuVhostUser] = enable_gpu_vhost_user;
}
bool CuttlefishConfig::InstanceSpecific::enable_gpu_vhost_user() const {
  return (*Dictionary())[kEnableGpuVhostUser].asBool();
}

static constexpr char kEnableGpuExternalBlob[] = "enable_gpu_external_blob";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_gpu_external_blob(
    const bool enable_gpu_external_blob) {
  (*Dictionary())[kEnableGpuExternalBlob] = enable_gpu_external_blob;
}
bool CuttlefishConfig::InstanceSpecific::enable_gpu_external_blob() const {
  return (*Dictionary())[kEnableGpuExternalBlob].asBool();
}

static constexpr char kEnableGpuSystemBlob[] = "enable_gpu_system_blob";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_gpu_system_blob(
    const bool enable_gpu_system_blob) {
  (*Dictionary())[kEnableGpuSystemBlob] = enable_gpu_system_blob;
}
bool CuttlefishConfig::InstanceSpecific::enable_gpu_system_blob() const {
  return (*Dictionary())[kEnableGpuSystemBlob].asBool();
}

static constexpr char kEnableAudio[] = "enable_audio";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_audio(bool enable) {
  (*Dictionary())[kEnableAudio] = enable;
}
bool CuttlefishConfig::InstanceSpecific::enable_audio() const {
  return (*Dictionary())[kEnableAudio].asBool();
}

static constexpr char kEnableGnssGrpcProxy[] = "enable_gnss_grpc_proxy";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_gnss_grpc_proxy(const bool enable_gnss_grpc_proxy) {
  (*Dictionary())[kEnableGnssGrpcProxy] = enable_gnss_grpc_proxy;
}
bool CuttlefishConfig::InstanceSpecific::enable_gnss_grpc_proxy() const {
  return (*Dictionary())[kEnableGnssGrpcProxy].asBool();
}

static constexpr char kEnableBootAnimation[] = "enable_bootanimation";
bool CuttlefishConfig::InstanceSpecific::enable_bootanimation() const {
  return (*Dictionary())[kEnableBootAnimation].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_enable_bootanimation(
    bool enable_bootanimation) {
  (*Dictionary())[kEnableBootAnimation] = enable_bootanimation;
}

static constexpr char kEnableUsb[] = "enable_usb";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_usb(bool enable) {
  (*Dictionary())[kEnableUsb] = enable;
}
bool CuttlefishConfig::InstanceSpecific::enable_usb() const {
  return (*Dictionary())[kEnableUsb].asBool();
}

static constexpr char kExtraBootconfigArgsInstanced[] = "extra_bootconfig_args";
std::vector<std::string>
CuttlefishConfig::InstanceSpecific::extra_bootconfig_args() const {
  std::string extra_bootconfig_args_str =
      (*Dictionary())[kExtraBootconfigArgsInstanced].asString();
  std::vector<std::string> bootconfig;
  if (!extra_bootconfig_args_str.empty()) {
    for (const auto& arg :
         android::base::Split(extra_bootconfig_args_str, " ")) {
      bootconfig.push_back(arg);
    }
  }
  return bootconfig;
}

void CuttlefishConfig::MutableInstanceSpecific::set_extra_bootconfig_args(
    const std::string& transport) {
  (*Dictionary())[kExtraBootconfigArgsInstanced] = transport;
}

static constexpr char kRecordScreen[] = "record_screen";
void CuttlefishConfig::MutableInstanceSpecific::set_record_screen(
    bool record_screen) {
  (*Dictionary())[kRecordScreen] = record_screen;
}
bool CuttlefishConfig::InstanceSpecific::record_screen() const {
  return (*Dictionary())[kRecordScreen].asBool();
}

static constexpr char kGem5DebugFile[] = "gem5_debug_file";
std::string CuttlefishConfig::InstanceSpecific::gem5_debug_file() const {
  return (*Dictionary())[kGem5DebugFile].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_gem5_debug_file(const std::string& gem5_debug_file) {
  (*Dictionary())[kGem5DebugFile] = gem5_debug_file;
}

static constexpr char kProtectedVm[] = "protected_vm";
void CuttlefishConfig::MutableInstanceSpecific::set_protected_vm(bool protected_vm) {
  (*Dictionary())[kProtectedVm] = protected_vm;
}
bool CuttlefishConfig::InstanceSpecific::protected_vm() const {
  return (*Dictionary())[kProtectedVm].asBool();
}

static constexpr char kMte[] = "mte";
void CuttlefishConfig::MutableInstanceSpecific::set_mte(bool mte) {
  (*Dictionary())[kMte] = mte;
}
bool CuttlefishConfig::InstanceSpecific::mte() const {
  return (*Dictionary())[kMte].asBool();
}

static constexpr char kEnableKernelLog[] = "enable_kernel_log";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_kernel_log(bool enable_kernel_log) {
  (*Dictionary())[kEnableKernelLog] = enable_kernel_log;
}
bool CuttlefishConfig::InstanceSpecific::enable_kernel_log() const {
  return (*Dictionary())[kEnableKernelLog].asBool();
}

static constexpr char kBootSlot[] = "boot_slot";
void CuttlefishConfig::MutableInstanceSpecific::set_boot_slot(const std::string& boot_slot) {
  (*Dictionary())[kBootSlot] = boot_slot;
}
std::string CuttlefishConfig::InstanceSpecific::boot_slot() const {
  return (*Dictionary())[kBootSlot].asString();
}

static constexpr char kFailFast[] = "fail_fast";
void CuttlefishConfig::MutableInstanceSpecific::set_fail_fast(bool fail_fast) {
  (*Dictionary())[kFailFast] = fail_fast;
}
bool CuttlefishConfig::InstanceSpecific::fail_fast() const {
  return (*Dictionary())[kFailFast].asBool();
}

static constexpr char kEnableWebRTC[] = "enable_webrtc";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_webrtc(bool enable_webrtc) {
  (*Dictionary())[kEnableWebRTC] = enable_webrtc;
}
bool CuttlefishConfig::InstanceSpecific::enable_webrtc() const {
  return (*Dictionary())[kEnableWebRTC].asBool();
}

static constexpr char kWebRTCAssetsDir[] = "webrtc_assets_dir";
void CuttlefishConfig::MutableInstanceSpecific::set_webrtc_assets_dir(const std::string& webrtc_assets_dir) {
  (*Dictionary())[kWebRTCAssetsDir] = webrtc_assets_dir;
}
std::string CuttlefishConfig::InstanceSpecific::webrtc_assets_dir() const {
  return (*Dictionary())[kWebRTCAssetsDir].asString();
}

static constexpr char kWebrtcTcpPortRange[] = "webrtc_tcp_port_range";
void CuttlefishConfig::MutableInstanceSpecific::set_webrtc_tcp_port_range(
    std::pair<uint16_t, uint16_t> range) {
  Json::Value arr(Json::ValueType::arrayValue);
  arr[0] = range.first;
  arr[1] = range.second;
  (*Dictionary())[kWebrtcTcpPortRange] = arr;
}
std::pair<uint16_t, uint16_t> CuttlefishConfig::InstanceSpecific::webrtc_tcp_port_range() const {
  std::pair<uint16_t, uint16_t> ret;
  ret.first = (*Dictionary())[kWebrtcTcpPortRange][0].asInt();
  ret.second = (*Dictionary())[kWebrtcTcpPortRange][1].asInt();
  return ret;
}

static constexpr char kWebrtcUdpPortRange[] = "webrtc_udp_port_range";
void CuttlefishConfig::MutableInstanceSpecific::set_webrtc_udp_port_range(
    std::pair<uint16_t, uint16_t> range) {
  Json::Value arr(Json::ValueType::arrayValue);
  arr[0] = range.first;
  arr[1] = range.second;
  (*Dictionary())[kWebrtcUdpPortRange] = arr;
}
std::pair<uint16_t, uint16_t> CuttlefishConfig::InstanceSpecific::webrtc_udp_port_range() const {
  std::pair<uint16_t, uint16_t> ret;
  ret.first = (*Dictionary())[kWebrtcUdpPortRange][0].asInt();
  ret.second = (*Dictionary())[kWebrtcUdpPortRange][1].asInt();
  return ret;
}

static constexpr char kGrpcConfig[] = "grpc_config";
std::string CuttlefishConfig::InstanceSpecific::grpc_socket_path() const {
  return (*Dictionary())[kGrpcConfig].asString();
}

void CuttlefishConfig::MutableInstanceSpecific::set_grpc_socket_path(
    const std::string& socket_path) {
  (*Dictionary())[kGrpcConfig] = socket_path;
}

static constexpr char kSmt[] = "smt";
void CuttlefishConfig::MutableInstanceSpecific::set_smt(bool smt) {
  (*Dictionary())[kSmt] = smt;
}
bool CuttlefishConfig::InstanceSpecific::smt() const {
  return (*Dictionary())[kSmt].asBool();
}

static constexpr char kCrosvmBinary[] = "crosvm_binary";
std::string CuttlefishConfig::InstanceSpecific::crosvm_binary() const {
  return (*Dictionary())[kCrosvmBinary].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_crosvm_binary(
    const std::string& crosvm_binary) {
  (*Dictionary())[kCrosvmBinary] = crosvm_binary;
}

void CuttlefishConfig::MutableInstanceSpecific::SetPath(
    const std::string& key, const std::string& path) {
  if (!path.empty()) {
    (*Dictionary())[key] = AbsolutePath(path);
  }
}

static constexpr char kSeccompPolicyDir[] = "seccomp_policy_dir";
void CuttlefishConfig::MutableInstanceSpecific::set_seccomp_policy_dir(
    const std::string& seccomp_policy_dir) {
  if (seccomp_policy_dir.empty()) {
    (*Dictionary())[kSeccompPolicyDir] = seccomp_policy_dir;
    return;
  }
  SetPath(kSeccompPolicyDir, seccomp_policy_dir);
}
std::string CuttlefishConfig::InstanceSpecific::seccomp_policy_dir() const {
  return (*Dictionary())[kSeccompPolicyDir].asString();
}

static constexpr char kQemuBinaryDir[] = "qemu_binary_dir";
std::string CuttlefishConfig::InstanceSpecific::qemu_binary_dir() const {
  return (*Dictionary())[kQemuBinaryDir].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_qemu_binary_dir(
    const std::string& qemu_binary_dir) {
  (*Dictionary())[kQemuBinaryDir] = qemu_binary_dir;
}

static constexpr char kVhostNet[] = "vhost_net";
void CuttlefishConfig::MutableInstanceSpecific::set_vhost_net(bool vhost_net) {
  (*Dictionary())[kVhostNet] = vhost_net;
}
bool CuttlefishConfig::InstanceSpecific::vhost_net() const {
  return (*Dictionary())[kVhostNet].asBool();
}

static constexpr char kVhostUserVsock[] = "vhost_user_vsock";
void CuttlefishConfig::MutableInstanceSpecific::set_vhost_user_vsock(
    bool vhost_user_vsock) {
  (*Dictionary())[kVhostUserVsock] = vhost_user_vsock;
}
bool CuttlefishConfig::InstanceSpecific::vhost_user_vsock() const {
  return (*Dictionary())[kVhostUserVsock].asBool();
}

static constexpr char kRilDns[] = "ril_dns";
void CuttlefishConfig::MutableInstanceSpecific::set_ril_dns(const std::string& ril_dns) {
  (*Dictionary())[kRilDns] = ril_dns;
}
std::string CuttlefishConfig::InstanceSpecific::ril_dns() const {
  return (*Dictionary())[kRilDns].asString();
}

static constexpr char kDisplayConfigs[] = "display_configs";
static constexpr char kXRes[] = "x_res";
static constexpr char kYRes[] = "y_res";
static constexpr char kDpi[] = "dpi";
static constexpr char kRefreshRateHz[] = "refresh_rate_hz";
std::vector<CuttlefishConfig::DisplayConfig>
CuttlefishConfig::InstanceSpecific::display_configs() const {
  std::vector<DisplayConfig> display_configs;
  for (auto& display_config_json : (*Dictionary())[kDisplayConfigs]) {
    DisplayConfig display_config = {};
    display_config.width = display_config_json[kXRes].asInt();
    display_config.height = display_config_json[kYRes].asInt();
    display_config.dpi = display_config_json[kDpi].asInt();
    display_config.refresh_rate_hz =
        display_config_json[kRefreshRateHz].asInt();
    display_configs.emplace_back(display_config);
  }
  return display_configs;
}
void CuttlefishConfig::MutableInstanceSpecific::set_display_configs(
    const std::vector<DisplayConfig>& display_configs) {
  Json::Value display_configs_json(Json::arrayValue);

  for (const DisplayConfig& display_configs : display_configs) {
    Json::Value display_config_json(Json::objectValue);
    display_config_json[kXRes] = display_configs.width;
    display_config_json[kYRes] = display_configs.height;
    display_config_json[kDpi] = display_configs.dpi;
    display_config_json[kRefreshRateHz] = display_configs.refresh_rate_hz;
    display_configs_json.append(display_config_json);
  }

  (*Dictionary())[kDisplayConfigs] = display_configs_json;
}

static constexpr char kTouchpadConfigs[] = "touchpad_configs";

Json::Value CuttlefishConfig::TouchpadConfig::Serialize(
    const CuttlefishConfig::TouchpadConfig& config) {
  Json::Value config_json(Json::objectValue);
  config_json[kXRes] = config.width;
  config_json[kYRes] = config.height;

  return config_json;
}

CuttlefishConfig::TouchpadConfig CuttlefishConfig::TouchpadConfig::Deserialize(
    const Json::Value& config_json) {
  TouchpadConfig touchpad_config = {};
  touchpad_config.width = config_json[kXRes].asInt();
  touchpad_config.height = config_json[kYRes].asInt();

  return touchpad_config;
}

std::vector<CuttlefishConfig::TouchpadConfig>
CuttlefishConfig::InstanceSpecific::touchpad_configs() const {
  std::vector<TouchpadConfig> touchpad_configs;
  for (auto& touchpad_config_json : (*Dictionary())[kTouchpadConfigs]) {
    auto touchpad_config = TouchpadConfig::Deserialize(touchpad_config_json);
    touchpad_configs.emplace_back(touchpad_config);
  }
  return touchpad_configs;
}
void CuttlefishConfig::MutableInstanceSpecific::set_touchpad_configs(
    const std::vector<TouchpadConfig>& touchpad_configs) {
  Json::Value touchpad_configs_json(Json::arrayValue);

  for (const TouchpadConfig& touchpad_config : touchpad_configs) {
    touchpad_configs_json.append(TouchpadConfig::Serialize(touchpad_config));
  }

  (*Dictionary())[kTouchpadConfigs] = touchpad_configs_json;
}

static constexpr char kTargetArch[] = "target_arch";
void CuttlefishConfig::MutableInstanceSpecific::set_target_arch(
    Arch target_arch) {
  (*Dictionary())[kTargetArch] = static_cast<int>(target_arch);
}
Arch CuttlefishConfig::InstanceSpecific::target_arch() const {
  return static_cast<Arch>((*Dictionary())[kTargetArch].asInt());
}

static constexpr char kEnableSandbox[] = "enable_sandbox";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_sandbox(const bool enable_sandbox) {
  (*Dictionary())[kEnableSandbox] = enable_sandbox;
}
bool CuttlefishConfig::InstanceSpecific::enable_sandbox() const {
  return (*Dictionary())[kEnableSandbox].asBool();
}
static constexpr char kEnableVirtiofs[] = "enable_virtiofs";
void CuttlefishConfig::MutableInstanceSpecific::set_enable_virtiofs(
    const bool enable_virtiofs) {
  (*Dictionary())[kEnableVirtiofs] = enable_virtiofs;
}
bool CuttlefishConfig::InstanceSpecific::enable_virtiofs() const {
  return (*Dictionary())[kEnableVirtiofs].asBool();
}
static constexpr char kConsole[] = "console";
void CuttlefishConfig::MutableInstanceSpecific::set_console(bool console) {
  (*Dictionary())[kConsole] = console;
}
bool CuttlefishConfig::InstanceSpecific::console() const {
  return (*Dictionary())[kConsole].asBool();
}
std::string CuttlefishConfig::InstanceSpecific::console_dev() const {
  auto can_use_virtio_console = !kgdb() && !use_bootloader();
  std::string console_dev;
  if (can_use_virtio_console ||
      config_->vm_manager() == vm_manager::Gem5Manager::name()) {
    // If kgdb and the bootloader are disabled, the Android serial console
    // spawns on a virtio-console port. If the bootloader is enabled, virtio
    // console can't be used since uboot doesn't support it.
    console_dev = "hvc1";
  } else {
    // QEMU and Gem5 emulate pl011 on ARM/ARM64, but QEMU and crosvm on other
    // architectures emulate ns16550a/uart8250 instead.
    Arch target = target_arch();
    if ((target == Arch::Arm64 || target == Arch::Arm) &&
        config_->vm_manager() != vm_manager::CrosvmManager::name()) {
      console_dev = "ttyAMA0";
    } else {
      console_dev = "ttyS0";
    }
  }
  return console_dev;
}

std::string CuttlefishConfig::InstanceSpecific::logcat_pipe_name() const {
  return AbsolutePath(PerInstanceInternalPath("logcat-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::restore_pipe_name() const {
  return AbsolutePath(PerInstanceInternalPath("restore-pipe"));
}

std::string CuttlefishConfig::InstanceSpecific::restore_adbd_pipe_name() const {
  return AbsolutePath(PerInstanceInternalPath("restore-pipe-adbd"));
}

std::string CuttlefishConfig::InstanceSpecific::access_kregistry_path() const {
  return AbsolutePath(PerInstancePath("access-kregistry"));
}

std::string CuttlefishConfig::InstanceSpecific::hwcomposer_pmem_path() const {
  return AbsolutePath(PerInstancePath("hwcomposer-pmem"));
}

std::string CuttlefishConfig::InstanceSpecific::pstore_path() const {
  return AbsolutePath(PerInstancePath("pstore"));
}

std::string CuttlefishConfig::InstanceSpecific::console_path() const {
  return AbsolutePath(PerInstancePath("console"));
}

std::string CuttlefishConfig::InstanceSpecific::logcat_path() const {
  return AbsolutePath(PerInstanceLogPath("logcat"));
}

std::string CuttlefishConfig::InstanceSpecific::launcher_monitor_socket_path()
    const {
  return AbsolutePath(PerInstanceUdsPath("launcher_monitor.sock"));
}

static constexpr char kModemSimulatorPorts[] = "modem_simulator_ports";
std::string CuttlefishConfig::InstanceSpecific::modem_simulator_ports() const {
  return (*Dictionary())[kModemSimulatorPorts].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_modem_simulator_ports(
    const std::string& modem_simulator_ports) {
  (*Dictionary())[kModemSimulatorPorts] = modem_simulator_ports;
}

std::string CuttlefishConfig::InstanceSpecific::launcher_log_path() const {
  return AbsolutePath(PerInstanceLogPath("launcher.log"));
}

std::string CuttlefishConfig::InstanceSpecific::metadata_image() const {
  return AbsolutePath(PerInstancePath("metadata.img"));
}

std::string CuttlefishConfig::InstanceSpecific::misc_image() const {
  return AbsolutePath(PerInstancePath("misc.img"));
}

std::string CuttlefishConfig::InstanceSpecific::sdcard_path() const {
  return AbsolutePath(PerInstancePath("sdcard.img"));
}

std::string CuttlefishConfig::InstanceSpecific::sdcard_overlay_path() const {
  return AbsolutePath(PerInstancePath("sdcard_overlay.img"));
}

std::string CuttlefishConfig::InstanceSpecific::persistent_composite_disk_path()
    const {
  return AbsolutePath(PerInstancePath("persistent_composite.img"));
}

std::string
CuttlefishConfig::InstanceSpecific::persistent_composite_overlay_path() const {
  return AbsolutePath(PerInstancePath("persistent_composite_overlay.img"));
}

std::string CuttlefishConfig::InstanceSpecific::persistent_ap_composite_disk_path()
    const {
  return AbsolutePath(PerInstancePath("ap_persistent_composite.img"));
}

std::string
CuttlefishConfig::InstanceSpecific::persistent_ap_composite_overlay_path()
    const {
  return AbsolutePath(PerInstancePath("ap_persistent_composite_overlay.img"));
}

std::string CuttlefishConfig::InstanceSpecific::os_composite_disk_path()
    const {
  return AbsolutePath(PerInstancePath("os_composite.img"));
}

std::string CuttlefishConfig::InstanceSpecific::ap_composite_disk_path()
    const {
  return AbsolutePath(PerInstancePath("ap_composite.img"));
}

std::string CuttlefishConfig::InstanceSpecific::vbmeta_path() const {
  return AbsolutePath(PerInstancePath("persistent_vbmeta.img"));
}

std::string CuttlefishConfig::InstanceSpecific::ap_vbmeta_path() const {
  return AbsolutePath(PerInstancePath("ap_vbmeta.img"));
}

std::string CuttlefishConfig::InstanceSpecific::uboot_env_image_path() const {
  return AbsolutePath(PerInstancePath("uboot_env.img"));
}

std::string CuttlefishConfig::InstanceSpecific::ap_uboot_env_image_path() const {
  return AbsolutePath(PerInstancePath("ap_uboot_env.img"));
}

std::string CuttlefishConfig::InstanceSpecific::chromeos_state_image() const {
  return AbsolutePath(PerInstancePath("chromeos_state.img"));
}

std::string CuttlefishConfig::InstanceSpecific::esp_image_path() const {
  return AbsolutePath(PerInstancePath("esp.img"));
}

std::string CuttlefishConfig::InstanceSpecific::ap_esp_image_path() const {
  return AbsolutePath(PerInstancePath("ap_esp.img"));
}

std::string CuttlefishConfig::InstanceSpecific::otheros_esp_grub_config() const {
  return AbsolutePath(PerInstancePath("grub.cfg"));
}

std::string CuttlefishConfig::InstanceSpecific::ap_esp_grub_config() const {
  return AbsolutePath(PerInstancePath("ap_grub.cfg"));
}

static constexpr char kMobileBridgeName[] = "mobile_bridge_name";

std::string CuttlefishConfig::InstanceSpecific::audio_server_path() const {
  return AbsolutePath(PerInstanceInternalUdsPath("audio_server.sock"));
}

CuttlefishConfig::InstanceSpecific::BootFlow CuttlefishConfig::InstanceSpecific::boot_flow() const {
  const bool android_efi_loader_flow_used = !android_efi_loader().empty();

  const bool chromeos_disk_flow_used = !chromeos_disk().empty();

  const bool chromeos_flow_used =
      !chromeos_kernel_path().empty() || !chromeos_root_image().empty();

  const bool linux_flow_used = !linux_kernel_path().empty()
    || !linux_initramfs_path().empty()
    || !linux_root_image().empty();

  const bool fuchsia_flow_used = !fuchsia_zedboot_path().empty()
    || !fuchsia_root_image().empty()
    || !fuchsia_multiboot_bin_path().empty();

  if (android_efi_loader_flow_used) {
    return BootFlow::AndroidEfiLoader;
  } else if (chromeos_flow_used) {
    return BootFlow::ChromeOs;
  } else if (chromeos_disk_flow_used) {
    return BootFlow::ChromeOsDisk;
  } else if (linux_flow_used) {
    return BootFlow::Linux;
  } else if (fuchsia_flow_used) {
    return BootFlow::Fuchsia;
  } else {
    return BootFlow::Android;
  }
 }

std::string CuttlefishConfig::InstanceSpecific::mobile_bridge_name() const {
  return (*Dictionary())[kMobileBridgeName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_bridge_name(
    const std::string& mobile_bridge_name) {
  (*Dictionary())[kMobileBridgeName] = mobile_bridge_name;
}

static constexpr char kMobileTapName[] = "mobile_tap_name";
std::string CuttlefishConfig::InstanceSpecific::mobile_tap_name() const {
  return (*Dictionary())[kMobileTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_tap_name(
    const std::string& mobile_tap_name) {
  (*Dictionary())[kMobileTapName] = mobile_tap_name;
}

static constexpr char kMobileMac[] = "mobile_mac";
std::string CuttlefishConfig::InstanceSpecific::mobile_mac() const {
  return (*Dictionary())[kMobileMac].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_mobile_mac(
    const std::string& mac) {
  (*Dictionary())[kMobileMac] = mac;
}

// TODO(b/199103204): remove this as well when
// PRODUCT_ENFORCE_MAC80211_HWSIM is removed
static constexpr char kWifiTapName[] = "wifi_tap_name";
std::string CuttlefishConfig::InstanceSpecific::wifi_tap_name() const {
  return (*Dictionary())[kWifiTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_wifi_tap_name(
    const std::string& wifi_tap_name) {
  (*Dictionary())[kWifiTapName] = wifi_tap_name;
}

static constexpr char kWifiBridgeName[] = "wifi_bridge_name";
std::string CuttlefishConfig::InstanceSpecific::wifi_bridge_name() const {
  return (*Dictionary())[kWifiBridgeName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_wifi_bridge_name(
    const std::string& wifi_bridge_name) {
  (*Dictionary())[kWifiBridgeName] = wifi_bridge_name;
}

static constexpr char kWifiMac[] = "wifi_mac";
std::string CuttlefishConfig::InstanceSpecific::wifi_mac() const {
  return (*Dictionary())[kWifiMac].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_wifi_mac(
    const std::string& mac) {
  (*Dictionary())[kWifiMac] = mac;
}

static constexpr char kUseBridgedWifiTap[] = "use_bridged_wifi_tap";
bool CuttlefishConfig::InstanceSpecific::use_bridged_wifi_tap() const {
  return (*Dictionary())[kUseBridgedWifiTap].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_use_bridged_wifi_tap(
    bool use_bridged_wifi_tap) {
  (*Dictionary())[kUseBridgedWifiTap] = use_bridged_wifi_tap;
}

static constexpr char kEthernetTapName[] = "ethernet_tap_name";
std::string CuttlefishConfig::InstanceSpecific::ethernet_tap_name() const {
  return (*Dictionary())[kEthernetTapName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_ethernet_tap_name(
    const std::string& ethernet_tap_name) {
  (*Dictionary())[kEthernetTapName] = ethernet_tap_name;
}

static constexpr char kEthernetBridgeName[] = "ethernet_bridge_name";
std::string CuttlefishConfig::InstanceSpecific::ethernet_bridge_name() const {
  return (*Dictionary())[kEthernetBridgeName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_ethernet_bridge_name(
    const std::string& ethernet_bridge_name) {
  (*Dictionary())[kEthernetBridgeName] = ethernet_bridge_name;
}

static constexpr char kEthernetMac[] = "ethernet_mac";
std::string CuttlefishConfig::InstanceSpecific::ethernet_mac() const {
  return (*Dictionary())[kEthernetMac].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_ethernet_mac(
    const std::string& mac) {
  (*Dictionary())[kEthernetMac] = mac;
}

static constexpr char kEthernetIPV6[] = "ethernet_ipv6";
std::string CuttlefishConfig::InstanceSpecific::ethernet_ipv6() const {
  return (*Dictionary())[kEthernetIPV6].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_ethernet_ipv6(
    const std::string& ip) {
  (*Dictionary())[kEthernetIPV6] = ip;
}

static constexpr char kUseAllocd[] = "use_allocd";
bool CuttlefishConfig::InstanceSpecific::use_allocd() const {
  return (*Dictionary())[kUseAllocd].asBool();
}
void CuttlefishConfig::MutableInstanceSpecific::set_use_allocd(
    bool use_allocd) {
  (*Dictionary())[kUseAllocd] = use_allocd;
}

static constexpr char kSessionId[] = "session_id";
uint32_t CuttlefishConfig::InstanceSpecific::session_id() const {
  return (*Dictionary())[kSessionId].asUInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_session_id(
    uint32_t session_id) {
  (*Dictionary())[kSessionId] = session_id;
}

static constexpr char kVsockGuestCid[] = "vsock_guest_cid";
int CuttlefishConfig::InstanceSpecific::vsock_guest_cid() const {
  return (*Dictionary())[kVsockGuestCid].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vsock_guest_cid(
    int vsock_guest_cid) {
  (*Dictionary())[kVsockGuestCid] = vsock_guest_cid;
}

static constexpr char kVsockGuestGroup[] = "vsock_guest_group";
std::string CuttlefishConfig::InstanceSpecific::vsock_guest_group() const {
  return (*Dictionary())[kVsockGuestGroup].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_vsock_guest_group(
    const std::string& vsock_guest_group) {
  (*Dictionary())[kVsockGuestGroup] = vsock_guest_group;
}

static constexpr char kUuid[] = "uuid";
std::string CuttlefishConfig::InstanceSpecific::uuid() const {
  return (*Dictionary())[kUuid].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_uuid(const std::string& uuid) {
  (*Dictionary())[kUuid] = uuid;
}

static constexpr char kEnvironmentName[] = "environment_name";
std::string CuttlefishConfig::InstanceSpecific::environment_name() const {
  return (*Dictionary())[kEnvironmentName].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_environment_name(
    const std::string& environment_name) {
  (*Dictionary())[kEnvironmentName] = environment_name;
}

std::string CuttlefishConfig::InstanceSpecific::CrosvmSocketPath() const {
  return PerInstanceInternalUdsPath("crosvm_control.sock");
}

std::string CuttlefishConfig::InstanceSpecific::OpenwrtCrosvmSocketPath()
    const {
  return PerInstanceInternalUdsPath("ap_control.sock");
}

static constexpr char kHostPort[] = "adb_host_port";
int CuttlefishConfig::InstanceSpecific::adb_host_port() const {
  return (*Dictionary())[kHostPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_adb_host_port(int port) {
  (*Dictionary())[kHostPort] = port;
}

static constexpr char kFastbootHostPort[] = "fastboot_host_port";
int CuttlefishConfig::InstanceSpecific::fastboot_host_port() const {
  return (*Dictionary())[kFastbootHostPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_fastboot_host_port(int port) {
  (*Dictionary())[kFastbootHostPort] = port;
}

static constexpr char kModemSimulatorId[] = "modem_simulator_host_id";
int CuttlefishConfig::InstanceSpecific::modem_simulator_host_id() const {
  return (*Dictionary())[kModemSimulatorId].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_modem_simulator_host_id(
    int id) {
  (*Dictionary())[kModemSimulatorId] = id;
}

static constexpr char kAdbIPAndPort[] = "adb_ip_and_port";
std::string CuttlefishConfig::InstanceSpecific::adb_ip_and_port() const {
  return (*Dictionary())[kAdbIPAndPort].asString();
}
void CuttlefishConfig::MutableInstanceSpecific::set_adb_ip_and_port(
    const std::string& ip_port) {
  (*Dictionary())[kAdbIPAndPort] = ip_port;
}

std::string CuttlefishConfig::InstanceSpecific::adb_device_name() const {
  if (adb_ip_and_port() != "") {
    return adb_ip_and_port();
  }
  LOG(ERROR) << "no adb_mode found, returning bad device name";
  return "NO_ADB_MODE_SET_NO_VALID_DEVICE_NAME";
}

static constexpr char kQemuVncServerPort[] = "qemu_vnc_server_port";
int CuttlefishConfig::InstanceSpecific::qemu_vnc_server_port() const {
  return (*Dictionary())[kQemuVncServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_qemu_vnc_server_port(
    int qemu_vnc_server_port) {
  (*Dictionary())[kQemuVncServerPort] = qemu_vnc_server_port;
}

static constexpr char kTombstoneReceiverPort[] = "tombstone_receiver_port";
int CuttlefishConfig::InstanceSpecific::tombstone_receiver_port() const {
  return (*Dictionary())[kTombstoneReceiverPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_tombstone_receiver_port(int tombstone_receiver_port) {
  (*Dictionary())[kTombstoneReceiverPort] = tombstone_receiver_port;
}

static constexpr char kAudioControlServerPort[] = "audiocontrol_server_port";
int CuttlefishConfig::InstanceSpecific::audiocontrol_server_port() const {
  return (*Dictionary())[kAudioControlServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_audiocontrol_server_port(int audiocontrol_server_port) {
  (*Dictionary())[kAudioControlServerPort] = audiocontrol_server_port;
}

static constexpr char kConfigServerPort[] = "config_server_port";
int CuttlefishConfig::InstanceSpecific::config_server_port() const {
  return (*Dictionary())[kConfigServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_config_server_port(int config_server_port) {
  (*Dictionary())[kConfigServerPort] = config_server_port;
}

static constexpr char kLightsServerPort[] = "lights_server_port";
int CuttlefishConfig::InstanceSpecific::lights_server_port() const {
  return (*Dictionary())[kLightsServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_lights_server_port(int lights_server_port) {
  (*Dictionary())[kLightsServerPort] = lights_server_port;
}

static constexpr char kCameraServerPort[] = "camera_server_port";
int CuttlefishConfig::InstanceSpecific::camera_server_port() const {
  return (*Dictionary())[kCameraServerPort].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_camera_server_port(
    int camera_server_port) {
  (*Dictionary())[kCameraServerPort] = camera_server_port;
}

static constexpr char kWebrtcDeviceId[] = "webrtc_device_id";
void CuttlefishConfig::MutableInstanceSpecific::set_webrtc_device_id(
    const std::string& id) {
  (*Dictionary())[kWebrtcDeviceId] = id;
}
std::string CuttlefishConfig::InstanceSpecific::webrtc_device_id() const {
  return (*Dictionary())[kWebrtcDeviceId].asString();
}

static constexpr char kGroupId[] = "group_id";
void CuttlefishConfig::MutableInstanceSpecific::set_group_id(
    const std::string& id) {
  (*Dictionary())[kGroupId] = id;
}
std::string CuttlefishConfig::InstanceSpecific::group_id() const {
  return (*Dictionary())[kGroupId].asString();
}

static constexpr char kStartSigServer[] = "webrtc_start_sig_server";
void CuttlefishConfig::MutableInstanceSpecific::set_start_webrtc_signaling_server(bool start) {
  (*Dictionary())[kStartSigServer] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_webrtc_sig_server() const {
  return (*Dictionary())[kStartSigServer].asBool();
}

static constexpr char kStartSigServerProxy[] = "webrtc_start_sig_server_proxy";
void CuttlefishConfig::MutableInstanceSpecific::
    set_start_webrtc_sig_server_proxy(bool start) {
  (*Dictionary())[kStartSigServerProxy] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_webrtc_sig_server_proxy() const {
  return (*Dictionary())[kStartSigServerProxy].asBool();
}

static constexpr char kStartRootcanal[] = "start_rootcanal";
void CuttlefishConfig::MutableInstanceSpecific::set_start_rootcanal(
    bool start) {
  (*Dictionary())[kStartRootcanal] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_rootcanal() const {
  return (*Dictionary())[kStartRootcanal].asBool();
}

static constexpr char kStartCasimir[] = "start_casimir";
void CuttlefishConfig::MutableInstanceSpecific::set_start_casimir(bool start) {
  (*Dictionary())[kStartCasimir] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_casimir() const {
  return (*Dictionary())[kStartCasimir].asBool();
}

static constexpr char kStartPica[] = "start_pica";
void CuttlefishConfig::MutableInstanceSpecific::set_start_pica(
    bool start) {
  (*Dictionary())[kStartPica] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_pica() const {
  return (*Dictionary())[kStartPica].asBool();
}

static constexpr char kStartNetsim[] = "start_netsim";
void CuttlefishConfig::MutableInstanceSpecific::set_start_netsim(bool start) {
  (*Dictionary())[kStartNetsim] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_netsim() const {
  return (*Dictionary())[kStartNetsim].asBool();
}

// TODO(b/288987294) Remove this when separating environment is done
static constexpr char kStartWmediumdInstance[] = "start_wmediumd_instance";
void CuttlefishConfig::MutableInstanceSpecific::set_start_wmediumd_instance(
    bool start) {
  (*Dictionary())[kStartWmediumdInstance] = start;
}
bool CuttlefishConfig::InstanceSpecific::start_wmediumd_instance() const {
  return (*Dictionary())[kStartWmediumdInstance].asBool();
}

static constexpr char kMcu[] = "mcu";
void CuttlefishConfig::MutableInstanceSpecific::set_mcu(const Json::Value& cfg) {
  (*Dictionary())[kMcu] = cfg;
}
const Json::Value& CuttlefishConfig::InstanceSpecific::mcu() const {
  return (*Dictionary())[kMcu];
}

static constexpr char kApBootFlow[] = "ap_boot_flow";
void CuttlefishConfig::MutableInstanceSpecific::set_ap_boot_flow(APBootFlow flow) {
  (*Dictionary())[kApBootFlow] = static_cast<int>(flow);
}
APBootFlow CuttlefishConfig::InstanceSpecific::ap_boot_flow() const {
  return static_cast<APBootFlow>((*Dictionary())[kApBootFlow].asInt());
}

static constexpr char kCrosvmUseBalloon[] = "crosvm_use_balloon";
void CuttlefishConfig::MutableInstanceSpecific::set_crosvm_use_balloon(
    const bool use_balloon) {
  (*Dictionary())[kCrosvmUseBalloon] = use_balloon;
}
bool CuttlefishConfig::InstanceSpecific::crosvm_use_balloon() const {
  return (*Dictionary())[kCrosvmUseBalloon].asBool();
}

static constexpr char kCrosvmUseRng[] = "crosvm_use_rng";
void CuttlefishConfig::MutableInstanceSpecific::set_crosvm_use_rng(
    const bool use_rng) {
  (*Dictionary())[kCrosvmUseRng] = use_rng;
}
bool CuttlefishConfig::InstanceSpecific::crosvm_use_rng() const {
  return (*Dictionary())[kCrosvmUseRng].asBool();
}

static constexpr char kCrosvmUsePmem[] = "use_pmem";
void CuttlefishConfig::MutableInstanceSpecific::set_use_pmem(
    const bool use_pmem) {
  (*Dictionary())[kCrosvmUsePmem] = use_pmem;
}
bool CuttlefishConfig::InstanceSpecific::use_pmem() const {
  return (*Dictionary())[kCrosvmUsePmem].asBool();
}

std::string CuttlefishConfig::InstanceSpecific::touch_socket_path(
    int touch_dev_idx) const {
  return PerInstanceInternalUdsPath(
      ("touch_" + std::to_string(touch_dev_idx) + ".sock").c_str());
}

std::string CuttlefishConfig::InstanceSpecific::rotary_socket_path() const {
  return PerInstanceInternalPath("rotary.sock");
}

std::string CuttlefishConfig::InstanceSpecific::keyboard_socket_path() const {
  return PerInstanceInternalUdsPath("keyboard.sock");
}

std::string CuttlefishConfig::InstanceSpecific::switches_socket_path() const {
  return PerInstanceInternalUdsPath("switches.sock");
}

std::string CuttlefishConfig::InstanceSpecific::frames_socket_path() const {
  return PerInstanceInternalUdsPath("frames.sock");
}

static constexpr char kWifiMacPrefix[] = "wifi_mac_prefix";
int CuttlefishConfig::InstanceSpecific::wifi_mac_prefix() const {
  return (*Dictionary())[kWifiMacPrefix].asInt();
}
void CuttlefishConfig::MutableInstanceSpecific::set_wifi_mac_prefix(
    int wifi_mac_prefix) {
  (*Dictionary())[kWifiMacPrefix] = wifi_mac_prefix;
}

std::string CuttlefishConfig::InstanceSpecific::factory_reset_protected_path() const {
  return PerInstanceInternalPath("factory_reset_protected.img");
}

std::string CuttlefishConfig::InstanceSpecific::persistent_bootconfig_path()
    const {
  return PerInstanceInternalPath("bootconfig");
}

std::string CuttlefishConfig::InstanceSpecific::PerInstancePath(
    const std::string& file_name) const {
  return (instance_dir() + "/") + file_name;
}

std::string CuttlefishConfig::InstanceSpecific::PerInstanceInternalPath(
    const std::string& file_name) const {
  if (file_name[0] == '\0') {
    // Don't append a / if file_name is empty.
    return PerInstancePath(kInternalDirName);
  }
  auto relative_path = (std::string(kInternalDirName) + "/") + file_name;
  return PerInstancePath(relative_path.c_str());
}

std::string CuttlefishConfig::InstanceSpecific::PerInstanceUdsPath(
    const std::string& file_name) const {
  return (instance_uds_dir() + "/") + file_name;
}

std::string CuttlefishConfig::InstanceSpecific::PerInstanceInternalUdsPath(
    const std::string& file_name) const {
  if (file_name[0] == '\0') {
    // Don't append a / if file_name is empty.
    return PerInstanceUdsPath(kInternalDirName);
  }
  auto relative_path = (std::string(kInternalDirName) + "/") + file_name;
  return PerInstanceUdsPath(relative_path.c_str());
}

std::string CuttlefishConfig::InstanceSpecific::PerInstanceGrpcSocketPath(
    const std::string& socket_name) const {
  if (socket_name.size() == 0) {
    // Don't append a / if file_name is empty.
    return PerInstanceUdsPath(kGrpcSocketDirName);
  }
  auto relative_path = (std::string(kGrpcSocketDirName) + "/") + socket_name;
  return PerInstanceUdsPath(relative_path.c_str());
}

std::string CuttlefishConfig::InstanceSpecific::PerInstanceLogPath(
    const std::string& file_name) const {
  if (file_name.size() == 0) {
    // Don't append a / if file_name is empty.
    return PerInstancePath(kLogDirName);
  }
  auto relative_path = (std::string(kLogDirName) + "/") + file_name;
  return PerInstancePath(relative_path.c_str());
}

std::string CuttlefishConfig::InstanceSpecific::instance_name() const {
  return IdToName(id_);
}

std::string CuttlefishConfig::InstanceSpecific::id() const { return id_; }

}  // namespace cuttlefish

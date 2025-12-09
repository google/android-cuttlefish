/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/file_source.h"

#include <ostream>
#include <string_view>

#include "absl/strings/match.h"

namespace cuttlefish {

FileSource SourceStringToEnum(std::string_view source) {
  if (absl::EqualsIgnoreCase(source, "default_build")) {
    return FileSource::DEFAULT_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "system_build")) {
    return FileSource::SYSTEM_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "kernel_build")) {
    return FileSource::KERNEL_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "local_file")) {
    return FileSource::LOCAL_FILE;
  } else if (absl::EqualsIgnoreCase(source, "generated")) {
    return FileSource::GENERATED;
  } else if (absl::EqualsIgnoreCase(source, "bootloader_build")) {
    return FileSource::BOOTLOADER_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "android_efi_loader_build")) {
    return FileSource::ANDROID_EFI_LOADER_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "boot_build")) {
    return FileSource::BOOT_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "host_package_build")) {
    return FileSource::HOST_PACKAGE_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "chrome_os_build")) {
    return FileSource::CHROME_OS_BUILD;
  } else if (absl::EqualsIgnoreCase(source, "test_suites_build")) {
    return FileSource::TEST_SUITES_BUILD;
  } else {
    return FileSource::UNKNOWN_PURPOSE;
  }
}

std::string_view SourceEnumToString(FileSource source) {
  if (source == FileSource::DEFAULT_BUILD) {
    return "default_build";
  } else if (source == FileSource::SYSTEM_BUILD) {
    return "system_build";
  } else if (source == FileSource::KERNEL_BUILD) {
    return "kernel_build";
  } else if (source == FileSource::LOCAL_FILE) {
    return "local_file";
  } else if (source == FileSource::GENERATED) {
    return "generated";
  } else if (source == FileSource::BOOTLOADER_BUILD) {
    return "bootloader_build";
  } else if (source == FileSource::ANDROID_EFI_LOADER_BUILD) {
    return "android_efi_loader_build";
  } else if (source == FileSource::BOOT_BUILD) {
    return "boot_build";
  } else if (source == FileSource::HOST_PACKAGE_BUILD) {
    return "host_package_build";
  } else if (source == FileSource::CHROME_OS_BUILD) {
    return "chrome_os_build";
  } else if (source == FileSource::TEST_SUITES_BUILD) {
    return "test_suites_build";
  } else {
    return "unknown";
  }
}

std::ostream& operator<<(std::ostream& out, FileSource source) {
  return out << SourceEnumToString(source);
}

}  // namespace cuttlefish

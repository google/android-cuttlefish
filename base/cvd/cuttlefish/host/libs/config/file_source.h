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
#pragma once

#include <ostream>
#include <string_view>

namespace cuttlefish {

/**
 * When a file is downloaded from `cvd fetch`, this is the "reason" the file is
 * downloaded. More specifically, whether it corresponds to the
 * `--default_build` argument, the `--system_build` argument, `--kernel_build`,
 * et cetera.
 *
 * Order in enum is not guaranteed to be stable, serialized as a string.
 */
enum class FileSource {
  UNKNOWN_PURPOSE = 0,
  DEFAULT_BUILD,
  SYSTEM_BUILD,
  KERNEL_BUILD,
  LOCAL_FILE,
  GENERATED,
  BOOTLOADER_BUILD,
  ANDROID_EFI_LOADER_BUILD,
  BOOT_BUILD,
  HOST_PACKAGE_BUILD,
  CHROME_OS_BUILD,
  TEST_SUITES_BUILD,
};

FileSource SourceStringToEnum(std::string_view source);
std::string_view SourceEnumToString(FileSource source);

std::ostream& operator<<(std::ostream&, FileSource);

template <typename Sink>
void AbslStringify(Sink& sink, FileSource file_source) {
  sink.Append(SourceEnumToString(file_source));
}

// For libfmt
std::string_view format_as(FileSource);

}  // namespace cuttlefish

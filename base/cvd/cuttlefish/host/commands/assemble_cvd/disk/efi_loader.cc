/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/disk/efi_loader.h"

#include <optional>
#include <string>
#include <utility>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

static constexpr std::string_view kName = "android_esp";

std::optional<EfiLoaderImage> EfiLoaderImage::Create(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (instance.boot_flow() != BootFlow::AndroidEfiLoader) {
    return std::nullopt;
  }
  return EfiLoaderImage(instance.esp_image_path());
}

std::string EfiLoaderImage::Name() const { return std::string(kName); }

Result<std::string> EfiLoaderImage::Generate() { return path_; }

Result<std::string> EfiLoaderImage::Path() const { return path_; }

EfiLoaderImage::EfiLoaderImage(std::string path) : path_(std::move(path)) {}

}  // namespace cuttlefish

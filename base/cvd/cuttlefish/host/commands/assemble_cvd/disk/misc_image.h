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

#pragma once

#include "cuttlefish/host/commands/assemble_cvd/disk/image_file.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class MiscImage : public ImageFile {
 public:
  static constexpr std::string_view kName = "misc";

  MiscImage(const CuttlefishConfig::InstanceSpecific&);

  std::string Name() const override;

  Result<std::string> Generate() override;

  Result<std::string> Path() const override;

 private:
  MiscImage(std::string);

  bool ready_;
  std::string path_;
};

}  // namespace cuttlefish

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
#pragma once

namespace cuttlefish {

enum class APBootFlow {
  // Not starting AP at all (for example not the 1st instance)
  None,
  // Generating ESP and using U-Boot along with monolith binaris in the
  // grub-efi-arm64-bin (for arm64) and grub-efi-amd64-bin (x86_64) deb
  // packages to boot AP.
  Grub,
  // Using legacy way to boot AP in case we cannot generate ESP image.
  LegacyDirect,
};

}  // namespace cuttlefish

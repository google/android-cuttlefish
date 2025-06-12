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
  // Generating ESP and using U-BOOT to boot AP
  Grub,
  // Using legacy way to boot AP in case we cannot generate ESP image.
  // Currently we have only one case when we cannot do it. When users
  // have ubuntu bionic which doesn't have monolith binaris in the
  // grub-efi-arm64-bin (for arm64) and grub-efi-ia32-bin (x86) deb packages.
  // TODO(b/260337906): check is it possible to add grub binaries into the AOSP
  // to deliver the proper grub environment
  // TODO(b/260338443): use grub-mkimage from grub-common in case we cannot
  // overcome
  // legal issues
  LegacyDirect,
};

}  // namespace cuttlefish

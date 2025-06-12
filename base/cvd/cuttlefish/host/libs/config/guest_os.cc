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
#include "cuttlefish/host/libs/config/guest_os.h"

#include "cuttlefish/host/libs/config/boot_flow.h"

namespace cuttlefish {

GuestOs GuestOsFromBootFlow(BootFlow boot_flow) {
  switch (boot_flow) {
    case BootFlow::Android:
    case BootFlow::AndroidEfiLoader:
      return GuestOs::Android;
    case BootFlow::ChromeOs:
    case BootFlow::ChromeOsDisk:
      return GuestOs::ChromeOs;
    case BootFlow::Linux:
      return GuestOs::Linux;
    case BootFlow::Fuchsia:
      return GuestOs::Fuchsia;
      // Don't include a default case, this needs to fail when not all cases
      // are covered.
  }
}

}  // namespace cuttlefish

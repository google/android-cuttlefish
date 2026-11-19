//
// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "h2h_workaround.h"
#include "usb.h"

#define GOOGLE_VID 0x18d1
#define H2H_PID1 0x506d
#define H2H_PID2 0x27a7

bool is_h2h_device(usb_ifc_info* info) {
    return info->dev_vendor == GOOGLE_VID && (info->dev_product == H2H_PID1 ||
                                              info->dev_product == H2H_PID2);
}

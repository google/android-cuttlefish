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

#pragma once

#include "usb.h"

// TODO(b/441807482): Remove this workaround when h2h cable FW is updated
// The function returns true when workaround should be applied for given USB
// interface. The workaround is to not display h2h entries in
// "fastboot devices" output.
bool is_h2h_device(usb_ifc_info* info);

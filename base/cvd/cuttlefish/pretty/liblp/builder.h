//
// Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include "liblp/builder.h"

#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/struct.h"

namespace cuttlefish {

PrettyStruct Pretty(const android::fs_mgr::Extent&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const android::fs_mgr::LinearExtent&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const android::fs_mgr::ZeroExtent&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const android::fs_mgr::PartitionGroup&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const android::fs_mgr::Partition&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const android::fs_mgr::Interval&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

}  // namespace cuttlefish

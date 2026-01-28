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

#include "liblp/metadata_format.h"

#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/struct.h"

namespace cuttlefish {

PrettyStruct Pretty(const LpMetadataGeometry&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const LpMetadataTableDescriptor&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const LpMetadataHeader&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const LpMetadataPartition&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const LpMetadataExtent&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const LpMetadataPartitionGroup&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

PrettyStruct Pretty(const LpMetadataBlockDevice&,
                    PrettyAdlPlaceholder unused = PrettyAdlPlaceholder());

}  // namespace cuttlefish

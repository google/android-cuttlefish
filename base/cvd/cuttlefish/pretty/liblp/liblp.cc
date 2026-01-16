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

#include "cuttlefish/pretty/liblp/liblp.h"

#include "liblp/liblp.h"

#include "cuttlefish/pretty/liblp/metadata_format.h"
#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/pretty/vector.h"

namespace cuttlefish {

PrettyStruct Pretty(const android::fs_mgr::LpMetadata& metadata,
                    PrettyAdlPlaceholder) {
  return PrettyStruct("LpMetadata")
      .Member("geometry", metadata.geometry)
      .Member("header", metadata.header)
      .Member("partitions", metadata.partitions)
      .Member("extents", metadata.extents)
      .Member("groups", metadata.groups)
      .Member("block_devices", metadata.block_devices);
}

}  // namespace cuttlefish

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

#include "cuttlefish/pretty/liblp/builder.h"

#include "liblp/builder.h"

#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/string.h"
#include "cuttlefish/pretty/struct.h"
#include "cuttlefish/pretty/unique_ptr.h"
#include "cuttlefish/pretty/vector.h"

namespace cuttlefish {

using android::fs_mgr::Extent;
using android::fs_mgr::ExtentType;
using android::fs_mgr::Interval;
using android::fs_mgr::LinearExtent;
using android::fs_mgr::Partition;
using android::fs_mgr::PartitionGroup;
using android::fs_mgr::ZeroExtent;

PrettyStruct Pretty(const Extent& extent, PrettyAdlPlaceholder) {
  switch (extent.GetExtentType()) {
    case ExtentType::kZero:
      return Pretty(static_cast<const ZeroExtent&>(extent));
    case ExtentType::kLinear:
      return Pretty(static_cast<const LinearExtent&>(extent));
    default:
      return PrettyStruct("Extent")
          .Member("GetExtentType", extent.GetExtentType())
          .Member("num_sectors", extent.num_sectors());
  }
}

PrettyStruct Pretty(const LinearExtent& linear_extent, PrettyAdlPlaceholder) {
  return PrettyStruct("LinearExtent")
      .Member("physical_sector", linear_extent.physical_sector())
      .Member("end_sector", linear_extent.end_sector())
      .Member("device_index", linear_extent.device_index())
      .Member("num_sectors", linear_extent.num_sectors());
}

PrettyStruct Pretty(const ZeroExtent& zero_extent, PrettyAdlPlaceholder) {
  return PrettyStruct("ZeroExtent")
      .Member("num_sectors", zero_extent.num_sectors());
}

PrettyStruct Pretty(const PartitionGroup& partition_group,
                    PrettyAdlPlaceholder) {
  return PrettyStruct("partition_group")
      .Member("name", partition_group.name())
      .Member("maximum_size", partition_group.maximum_size());
}

PrettyStruct Pretty(const Partition& partition, PrettyAdlPlaceholder) {
  return PrettyStruct("Partition")
      .Member("BytesOnDisk", partition.BytesOnDisk())
      .Member("name", partition.name())
      .Member("group_name", partition.group_name())
      .Member("attributes", partition.attributes())
      .Member("extents", partition.extents())
      .Member("size", partition.size());
}

PrettyStruct Pretty(const Interval& interval, PrettyAdlPlaceholder) {
  return PrettyStruct("Interval")
      .Member("device_index", interval.device_index)
      .Member("start", interval.start)
      .Member("end", interval.end);
}

}  // namespace cuttlefish

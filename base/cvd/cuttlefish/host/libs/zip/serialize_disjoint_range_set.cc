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
#include "cuttlefish/host/libs/zip/serialize_disjoint_range_set.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/host/libs/zip/disjoint_range_set.h"
#include "cuttlefish/host/libs/zip/disjoint_range_set.pb.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

std::string Serialize(const DisjointRangeSet& range_set) {
  DisjointRangeList proto;
  for (std::pair<uint64_t, uint64_t> range : range_set.AllRanges()) {
    DisjointRangeListMember* member = proto.add_ranges();
    member->set_start(range.first);
    member->set_end(range.second);
  }
  return proto.SerializeAsString();
}

Result<DisjointRangeSet> DeserializeDisjointRangeSet(std::string_view data) {
  DisjointRangeList proto;
  CF_EXPECT(proto.ParseFromString(data));
  DisjointRangeSet set;
  for (const DisjointRangeListMember& range : proto.ranges()) {
    set.InsertRange(range.start(), range.end());
  }
  return set;
}

}  // namespace cuttlefish

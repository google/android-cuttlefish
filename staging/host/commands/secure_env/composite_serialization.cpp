//
// Copyright (C) 2020 The Android Open Source Project
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

#include "composite_serialization.h"

using keymaster::Serializable;

CompositeSerializable::CompositeSerializable(
    const std::vector<Serializable*>& members) : members_(members) {
}

size_t CompositeSerializable::SerializedSize() const {
  size_t sum = 0;
  for (const auto& member : members_) {
    sum += member->SerializedSize();
  }
  return sum;
}

uint8_t* CompositeSerializable::Serialize(
    uint8_t* buf, const uint8_t* end) const {
  for (const auto& member : members_) {
    buf = member->Serialize(buf, end);
  }
  return buf;
}

bool CompositeSerializable::Deserialize(
    const uint8_t** buf_ptr, const uint8_t* end) {
  for (const auto& member : members_) {
    if (!member->Deserialize(buf_ptr, end)) {
      return false;
    }
  }
  return true;
}

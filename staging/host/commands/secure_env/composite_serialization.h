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

#pragma once

#include <vector>

#include "keymaster/serializable.h"

/**
 * A keymaster::Serializable type that refers to multiple other
 * keymaster::Serializable instances by pointer. When data is serialized or
 * deserialized, it's delegated to the instances pointed to.
 *
 * The serialization format is to put the instances one after the other in
 * the byte stream.
 */
class CompositeSerializable : public keymaster::Serializable {
public:
  CompositeSerializable(const std::vector<keymaster::Serializable*>&);

  size_t SerializedSize() const override;
  uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const override;
  bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) override;
private:
  std::vector<keymaster::Serializable*> members_;
};

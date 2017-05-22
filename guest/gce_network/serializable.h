/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef GUEST_GCE_NETWORK_SERIALIZABLE_H_
#define GUEST_GCE_NETWORK_SERIALIZABLE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace avd {
// Interface for serializable classes.
// Any class implementing this interface can be serialized to a single packet of
// data and reconstructed later on.
class Serializable {
 public:
  Serializable() {}
  virtual ~Serializable() {}

  // Compose a single packet of data from which the class can be reconstructed.
  virtual bool Serialize(std::vector<uint8_t>* data) const = 0;

  // Decompose a packet of data.
  virtual bool Deserialize(const std::vector<uint8_t>& data) = 0;

  // Consume |num_bytes| of data from the |vector| at offset |offset|.
  // |offset| is an in-out parameter.
  // Bytes are stored in |target|.
  // Returns true if vector held at least |num_bytes| of data after |offset|.
  static bool ConsumeBytes(
      const std::vector<uint8_t>& vector, size_t* offset,
      uint8_t* target, size_t num_bytes);

  // Skip |bytes| of data from |vector| starting at |offset|.
  // Returns true, if |vector| had at least |offset+bytes| elements.
  bool SkipBytes(
      const std::vector<uint8_t>& vector, size_t* offset, size_t bytes);

  // Convert next |num_bytes| to an integer. Serialized number is assumed to be
  // in network byte order (big endian).
  // Converted value will be stored in location pointed to by |target|.
  // Returns true, if |vector| had at least |offset+num_bytes| elements.
  bool ConsumeInt(
      const std::vector<uint8_t>& vector, size_t* offset,
      uint32_t* target, size_t num_bytes);

  // Serialize |num_bytes| of |data| to |vector|.
  static void AppendBytes(
      std::vector<uint8_t>* vector, const uint8_t* data, size_t num_bytes);

  // Serialize |num_bytes| long |value| to |vector|.
  // Value will be stored in network byte order (big endian).
  static void AppendInt(
      std::vector<uint8_t>* vector, uint32_t value, size_t num_bytes);

  // Write |num_bytes| of pad data (\0) to |vector|.
  static void PadBytes(
      std::vector<uint8_t>* vector, size_t num_bytes);
};

}  // namespace avd

#endif  // GUEST_GCE_NETWORK_SERIALIZABLE_H_

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
#include "serializable.h"

#include <arpa/inet.h>

#include "logging.h"

namespace avd {

bool Serializable::ConsumeInt(
    const std::vector<uint8_t>& vector, size_t* offset,
    uint32_t* target, size_t num_bytes) {
  // Abort immediately if not enough data.
  if (*offset + num_bytes > vector.size()) return false;
  // Just in case.
  if (num_bytes > sizeof(*target)) return false;

  *target = 0;

  // Always create 32bit; simplifies network to host byte order conversion.
  while (num_bytes--) {
    *target <<= 8;
    *target |= vector[(*offset)++];
  }

  return true;
}

bool Serializable::ConsumeBytes(
    const std::vector<uint8_t>& vector, size_t* offset,
    uint8_t* target, size_t num_bytes) {
  // Abort immediately if not enough data.
  if (*offset + num_bytes > vector.size()) return false;

  while (num_bytes--) {
    *target++ = vector[(*offset)++];
  }

  return true;
}

bool Serializable::SkipBytes(
    const std::vector<uint8_t>& vector, size_t* offset, size_t num_bytes) {
  // Abort immediately if not enough data.
  if (*offset + num_bytes > vector.size()) return false;

  *offset += num_bytes;
  return true;
}

void Serializable::AppendBytes(
      std::vector<uint8_t>* vector, const uint8_t* data, size_t num_bytes) {
  while (num_bytes--) vector->push_back(*data++);
}

void Serializable::AppendInt(
    std::vector<uint8_t>* vector, uint32_t value, size_t num_bytes) {
  // Make sure the first relevant byte is the most significant one.
  // This simplifies storage.
  for (size_t adjust = num_bytes; adjust < sizeof(value); ++adjust) {
    value <<= 8;
  }

  // Append most significant byte of value num_bytes times.
  while (num_bytes--) {
    vector->push_back(value >> 24);
    value <<= 8;
  }
}

void Serializable::PadBytes(
    std::vector<uint8_t>* vector, size_t num_bytes) {
  vector->resize(vector->size() + num_bytes, 0);
}

}  // namespace avd

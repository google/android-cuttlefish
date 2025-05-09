/*
 * Copyright 2023 The Android Open Source Project
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

/*
* TODO: b/416777029 - Stop using this copy of the file
*/

#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace pdl::packet {

/// Representation of a raw packet slice.
/// The slice contains a shared pointer to the source packet bytes, and points
/// to a subrange within this byte buffer.
class slice {
 public:
  slice() = default;
  slice(slice const&) = default;
  slice(std::shared_ptr<const std::vector<uint8_t>> packet)
      : packet_(std::move(packet)), offset_(0), size_(packet_->size()) {}

  slice(std::shared_ptr<const std::vector<uint8_t>> packet, size_t offset,
        size_t size)
      : packet_(std::move(packet)), offset_(offset), size_(size) {}

  /// Return a new slice that contains the selected subrange within the
  /// current slice. The range ['offset', 'offset' + 'slice') must be
  /// contained within the bonuds of the current slice.
  slice subrange(size_t offset, size_t size) const {
    assert((offset + size) <= size_);
    return slice(packet_, offset_ + offset, size);
  }

  /// Read a scalar value encoded in little-endian.
  /// The bytes that are read from calling this function are consumed.
  /// This function can be used to iterativaly extract values from a packet
  /// slice.
  template <typename T, size_t N = sizeof(T)>
  T read_le() {
    static_assert(N <= sizeof(T));
    assert(N <= size_);
    T value = 0;
    for (size_t n = 0; n < N; n++) {
      value |= (T)at(n) << (8 * n);
    }
    skip(N);
    return value;
  }

  /// Read a scalar value encoded in big-endian.
  /// The bytes that are read from calling this function are consumed.
  /// This function can be used to iterativaly extract values from a packet
  /// slice.
  template <typename T, size_t N = sizeof(T)>
  T read_be() {
    static_assert(N <= sizeof(T));
    assert(N <= size_);
    T value = 0;
    for (size_t n = 0; n < N; n++) {
      value = (value << 8) | (T)at(n);
    }
    skip(N);
    return value;
  }

  /// Return the value of the byte at the given offset.
  /// `offset` must be within the bounds of the slice.
  uint8_t at(size_t offset) const {
    assert(offset <= size_);
    return packet_->at(offset_ + offset);
  }

  /// Skip `size` bytes at the front of the slice.
  /// `size` must be lower than or equal to the slice size.
  void skip(size_t size) {
    assert(size <= size_);
    offset_ += size;
    size_ -= size;
  }

  /// Empty the slice.
  void clear() { size_ = 0; }

  /// Return the size of the slice in bytes.
  size_t size() const { return size_; }

  /// Return the contents of the slice as a byte vector.
  std::vector<uint8_t> bytes() const {
    return std::vector<uint8_t>(packet_->cbegin() + offset_,
                                packet_->cbegin() + offset_ + size_);
  }

  bool operator==(slice const& other) const {
    return size_ == other.size_ &&
           std::equal(packet_->begin() + offset_,
                      packet_->begin() + offset_ + size_,
                      other.packet_->begin());
  }

 private:
  std::shared_ptr<const std::vector<uint8_t>> packet_;
  size_t offset_{0};
  size_t size_{0};
};

/// Interface class for generated packet builders.
class Builder {
 public:
  virtual ~Builder() = default;

  /// Method implemented by generated packet builders.
  /// The packet fields are concatenated to the output vector.
  virtual void Serialize(std::vector<uint8_t>&) const {}

  /// Method implemented by generated packet builders.
  /// Returns the size of the serialized packet in bytes.
  virtual size_t GetSize() const { return 0; }

  /// Write a scalar value encoded in little-endian.
  template <typename T, size_t N = sizeof(T)>
  static void write_le(std::vector<uint8_t>& output, T value) {
    static_assert(N <= sizeof(T));
    for (size_t n = 0; n < N; n++) {
      output.push_back(value >> (8 * n));
    }
  }

  /// Write a scalar value encoded in big-endian.
  template <typename T, size_t N = sizeof(T)>
  static void write_be(std::vector<uint8_t>& output, T value) {
    static_assert(N <= sizeof(T));
    for (size_t n = 0; n < N; n++) {
      output.push_back(value >> (8 * (N - 1 - n)));
    }
  }

  /// Helper method to serialize the packet to a byte vector.
  virtual std::vector<uint8_t> SerializeToBytes() const {
    std::vector<uint8_t> output;
    Serialize(output);
    return output;
  }
};

}  // namespace pdl::packet

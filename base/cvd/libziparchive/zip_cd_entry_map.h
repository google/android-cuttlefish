/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <string_view>
#include <utility>

#include <android-base/logging.h>
#include <log/log.h>

#include "zip_error.h"

/*
 * Round up to the next highest power of 2.
 *
 * Found on http://graphics.stanford.edu/~seander/bithacks.html.
 *
 * TODO: could switch to use std::bit_ceil() once ready
 */
static constexpr uint32_t RoundUpPower2(uint32_t val) {
  val--;
  val |= val >> 1;
  val |= val >> 2;
  val |= val >> 4;
  val |= val >> 8;
  val |= val >> 16;
  val++;

  return val;
}

// This class is the interface of the central directory entries map. The map
// helps to locate a particular cd entry based on the filename.
class CdEntryMapInterface {
 public:
  virtual ~CdEntryMapInterface() = default;
  // Adds an entry to the map. The |name| should internally points to the
  // filename field of a cd entry. And |start| points to the beginning of the
  // central directory. Returns 0 on success.
  virtual ZipError AddToMap(std::string_view name, const uint8_t* start) = 0;
  // For the zip entry |entryName|, finds the offset of its filename field in
  // the central directory. Returns a pair of [status, offset]. The value of
  // the status is 0 on success.
  virtual std::pair<ZipError, uint64_t> GetCdEntryOffset(std::string_view name,
                                                         const uint8_t* cd_start) const = 0;
  // Resets the iterator to the beginning of the map.
  virtual void ResetIteration() = 0;
  // Returns the [name, cd offset] of the current element. Also increments the
  // iterator to points to the next element. Returns an empty pair we have read
  // past boundary.
  virtual std::pair<std::string_view, uint64_t> Next(const uint8_t* cd_start) = 0;

  static std::unique_ptr<CdEntryMapInterface> Create(uint64_t num_entries,
          size_t cd_length, uint16_t max_file_name_length);
};

/**
 * More space efficient string representation of strings in an mmaped zipped
 * file than std::string_view. Using std::string_view as an entry in the
 * ZipArchive hash table wastes space. std::string_view stores a pointer to a
 * string (on 64 bit, 8 bytes) and the length to read from that pointer,
 * 2 bytes. Because of alignment, the structure consumes 16 bytes, wasting
 * 6 bytes.
 */

/**
 * ZipStringOffset20 stores 20-bit for offset from a fixed location in the memory
 * mapped file instead of the entire address, with 12-bit for filename length (i.e.
 * typical PATH_MAX), consuming 4 bytes in total
 */
struct ZipStringOffset20 {
  static constexpr size_t offset_max = (1u << 20) - 1;
  static constexpr size_t length_max = (1u << 12) - 1;
  uint32_t name_offset : 20;
  uint16_t name_length : 12;
};

static_assert(sizeof(struct ZipStringOffset20) == 4);

/**
 * ZipStringOffset32 stores a 4 byte offset from a fixed location in the memory
 * mapped file instead of the entire address, consuming 8 bytes with alignment.
 */
struct ZipStringOffset32 {
  uint32_t name_offset;
  uint16_t name_length;
};

// This implementation of CdEntryMap uses an array hash table. It uses less
// memory than std::map; and it's used as the default implementation for zip
// archives without zip64 extension.
template <typename ZipStringOffset>
class CdEntryMapZip32 : public CdEntryMapInterface {
 public:
  ZipError AddToMap(std::string_view name, const uint8_t* start) override;
  std::pair<ZipError, uint64_t> GetCdEntryOffset(std::string_view name,
                                                 const uint8_t* cd_start) const override;
  void ResetIteration() override;
  std::pair<std::string_view, uint64_t> Next(const uint8_t* cd_start) override;

  explicit CdEntryMapZip32(uint16_t num_entries) {
    /*
     * Create hash table.  We have a minimum 75% load factor, possibly as
     * low as 50% after we round off to a power of 2.  There must be at
     * least one unused entry to avoid an infinite loop during creation.
     */
    hash_table_size_ = RoundUpPower2(1 + (num_entries * 4) / 3);
    hash_table_.reset(static_cast<ZipStringOffset*>(
        calloc(hash_table_size_, sizeof(ZipStringOffset))));

    CHECK(hash_table_ != nullptr)
      << "Zip: unable to allocate the " << hash_table_size_
      << " entry hash_table, entry size: " << sizeof(ZipStringOffset);
  }

  // We know how many entries are in the Zip archive, so we can have a
  // fixed-size hash table. We define a load factor of 0.75 and over
  // allocate so the maximum number entries can never be higher than
  // ((4 * UINT16_MAX) / 3 + 1) which can safely fit into a uint32_t.
  struct FreeDeleter {
    void operator()(void* ptr) const { ::free(ptr); }
  };
  std::unique_ptr<ZipStringOffset[], FreeDeleter> hash_table_;
  uint32_t hash_table_size_{0};

 private:
  // The position of element for the current iteration.
  uint32_t current_position_{0};
};

// This implementation of CdEntryMap uses a std::map
class CdEntryMapZip64 : public CdEntryMapInterface {
 public:
  ZipError AddToMap(std::string_view name, const uint8_t* start) override;
  std::pair<ZipError, uint64_t> GetCdEntryOffset(std::string_view name,
                                                 const uint8_t* cd_start) const override;
  void ResetIteration() override;
  std::pair<std::string_view, uint64_t> Next(const uint8_t* cd_start) override;

  CdEntryMapZip64() = default;
 private:

  std::map<std::string_view, uint64_t> entry_table_;

  std::map<std::string_view, uint64_t>::iterator iterator_;
};

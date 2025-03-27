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

#include "zip_cd_entry_map.h"

static uint32_t ComputeHash(std::string_view name) {
  return static_cast<uint32_t>(std::hash<std::string_view>{}(name));
}

template <typename ZipStringOffset>
const std::string_view ToStringView(ZipStringOffset& entry, const uint8_t *start) {
  auto name = reinterpret_cast<const char*>(start + entry.name_offset);
  return std::string_view{name, entry.name_length};
}

// Convert a ZipEntry to a hash table index, verifying that it's in a valid range.
template <typename ZipStringOffset>
std::pair<ZipError, uint64_t> CdEntryMapZip32<ZipStringOffset>::GetCdEntryOffset(
        std::string_view name, const uint8_t* start) const {
  const uint32_t hash = ComputeHash(name);

  // NOTE: (hash_table_size - 1) is guaranteed to be non-negative.
  uint32_t ent = hash & (hash_table_size_ - 1);
  while (hash_table_[ent].name_offset != 0) {
    if (ToStringView(hash_table_[ent], start) == name) {
      return {kSuccess, static_cast<uint64_t>(hash_table_[ent].name_offset)};
    }
    ent = (ent + 1) & (hash_table_size_ - 1);
  }

  ALOGV("Zip: Unable to find entry %.*s", static_cast<int>(name.size()), name.data());
  return {kEntryNotFound, 0};
}

template <typename ZipStringOffset>
ZipError CdEntryMapZip32<ZipStringOffset>::AddToMap(std::string_view name, const uint8_t* start) {
  const uint64_t hash = ComputeHash(name);
  uint32_t ent = hash & (hash_table_size_ - 1);

  /*
   * We over-allocated the table, so we're guaranteed to find an empty slot.
   * Further, we guarantee that the hashtable size is not 0.
   */
  while (hash_table_[ent].name_offset != 0) {
    if (ToStringView(hash_table_[ent], start) == name) {
      // We've found a duplicate entry. We don't accept duplicates.
      ALOGW("Zip: Found duplicate entry %.*s", static_cast<int>(name.size()), name.data());
      return kDuplicateEntry;
    }
    ent = (ent + 1) & (hash_table_size_ - 1);
  }

  // `name` has already been validated before entry.
  const char* start_char = reinterpret_cast<const char*>(start);
  hash_table_[ent].name_offset = static_cast<uint32_t>(name.data() - start_char);
  hash_table_[ent].name_length = static_cast<uint16_t>(name.size());
  return kSuccess;
}

template <typename ZipStringOffset>
void CdEntryMapZip32<ZipStringOffset>::ResetIteration() {
  current_position_ = 0;
}

template <typename ZipStringOffset>
std::pair<std::string_view, uint64_t> CdEntryMapZip32<ZipStringOffset>::Next(
        const uint8_t* cd_start) {
  while (current_position_ < hash_table_size_) {
    const auto& entry = hash_table_[current_position_];
    current_position_ += 1;

    if (entry.name_offset != 0) {
      return {ToStringView(entry, cd_start), static_cast<uint64_t>(entry.name_offset)};
    }
  }
  // We have reached the end of the hash table.
  return {};
}

ZipError CdEntryMapZip64::AddToMap(std::string_view name, const uint8_t* start) {
  const auto [it, added] =
      entry_table_.insert({name, name.data() - reinterpret_cast<const char*>(start)});
  if (!added) {
    ALOGW("Zip: Found duplicate entry %.*s", static_cast<int>(name.size()), name.data());
    return kDuplicateEntry;
  }
  return kSuccess;
}

std::pair<ZipError, uint64_t> CdEntryMapZip64::GetCdEntryOffset(std::string_view name,
                                                                const uint8_t* /*cd_start*/) const {
  const auto it = entry_table_.find(name);
  if (it == entry_table_.end()) {
    ALOGV("Zip: Could not find entry %.*s", static_cast<int>(name.size()), name.data());
    return {kEntryNotFound, 0};
  }

  return {kSuccess, it->second};
}

void CdEntryMapZip64::ResetIteration() {
  iterator_ = entry_table_.begin();
}

std::pair<std::string_view, uint64_t> CdEntryMapZip64::Next(const uint8_t* /*cd_start*/) {
  if (iterator_ == entry_table_.end()) {
    return {};
  }

  return *iterator_++;
}

std::unique_ptr<CdEntryMapInterface> CdEntryMapInterface::Create(uint64_t num_entries,
        size_t cd_length, uint16_t max_file_name_length) {
  using T = std::unique_ptr<CdEntryMapInterface>;
  if (num_entries > UINT16_MAX)
    return T(new CdEntryMapZip64());

  uint16_t num_entries_ = static_cast<uint16_t>(num_entries);
  if (cd_length > ZipStringOffset20::offset_max ||
      max_file_name_length > ZipStringOffset20::length_max) {
    return T(new CdEntryMapZip32<ZipStringOffset32>(num_entries_));
  }
  return T(new CdEntryMapZip32<ZipStringOffset20>(num_entries_));
}

/*
 * Copyright (C) 2026 The Android Open Source Project
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
#include "cuttlefish/host/libs/image_aggregator/composite_io.h"

#include <stdint.h>

#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

/* static */ Result<CompositeDiskReaderIo> CompositeDiskReaderIo::Create(
    CompositeDiskImage composite, ReadFilesystem& filesystem) {
  std::map<uint64_t, std::unique_ptr<ReaderSeeker>> offset_to_file;
  for (const ComponentDisk& member : composite.GetCompositeDisk().component_disks()) {
    std::unique_ptr<ReaderSeeker> file =
        CF_EXPECT(filesystem.OpenReadOnly(member.file_path()));
    CF_EXPECT(file.get());
    offset_to_file[member.offset()] = std::move(file);
  }
  CF_EXPECT_EQ(offset_to_file.count(0), 1, "No initial member");
  return CompositeDiskReaderIo(std::move(composite), std::move(offset_to_file));
}

CompositeDiskReaderIo::CompositeDiskReaderIo(
    CompositeDiskImage composite, std::map<uint64_t, std::unique_ptr<ReaderSeeker>> offset_to_file)
    : ReaderFakeSeeker(composite.GetCompositeDisk().length()),
      composite_(std::move(composite)),
      offset_to_file_(std::move(offset_to_file)) {}

Result<uint64_t> CompositeDiskReaderIo::PRead(void* buf, uint64_t count, uint64_t offset) const {
  const uint64_t full_length = composite_.GetCompositeDisk().length();
  auto it = offset_to_file_.upper_bound(offset);
  uint64_t read_end = it == offset_to_file_.end() ? full_length : it->first;
  CF_EXPECT(it != offset_to_file_.begin());
  it--;
  CF_EXPECT(it != offset_to_file_.end());
  CF_EXPECT_GE(offset, it->first);
  if (read_end < offset) {
    return 0;
  }
  if (offset + count > read_end) {
    count = read_end - offset;
  }
  CF_EXPECT(it->second.get());
  return CF_EXPECT(it->second->PRead(buf, count, offset - it->first));
}

}  // namespace cuttlefish

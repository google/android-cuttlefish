/*
 * Copyright (C) 2019 The Android Open Source Project
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
#pragma once

#include <stdint.h>

#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class CompositeDiskReaderIo : public ReaderFakeSeeker {
 public:
  static Result<CompositeDiskReaderIo> Create(CompositeDiskImage, ReadFilesystem&);

  CompositeDiskReaderIo(CompositeDiskReaderIo&&) = default;
  ~CompositeDiskReaderIo() = default;
  CompositeDiskReaderIo& operator=(CompositeDiskReaderIo&&) = default;

  Result<uint64_t> PRead(void* buf, uint64_t count, uint64_t offset) const override;
 private:
  CompositeDiskReaderIo(CompositeDiskImage, std::map<uint64_t, std::unique_ptr<ReaderSeeker>>);

  CompositeDiskImage composite_;
  std::map<uint64_t, std::unique_ptr<ReaderSeeker>> offset_to_file_;
};

}  // namespace cuttlefish

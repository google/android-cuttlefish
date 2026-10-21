//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/io/in_memory.h"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/io/in_memory_fs.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/owning_in_memory_io.h"

namespace cuttlefish {

std::unique_ptr<ReaderWriterSeeker> InMemoryIo() {
  return std::make_unique<OwningInMemoryIo>();
}

std::unique_ptr<ReaderWriterSeeker> InMemoryIo(std::string_view str) {
  std::vector<char> data(str.begin(), str.end());
  return std::make_unique<OwningInMemoryIo>(std::move(data));
}

std::unique_ptr<ReaderWriterSeeker> InMemoryIo(std::vector<char> data) {
  return std::make_unique<OwningInMemoryIo>(std::move(data));
}

std::unique_ptr<ReadWriteFilesystem> InMemoryFilesystem() {
  return std::make_unique<class InMemoryFilesystem>();
}

}  // namespace cuttlefish

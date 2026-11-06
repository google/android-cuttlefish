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

#include "cuttlefish/io/owning_in_memory_io.h"

#include <string_view>
#include <utility>
#include <vector>

#include "cuttlefish/io/in_memory_io.h"

namespace cuttlefish {

OwningInMemoryIo::OwningInMemoryIo() : InMemoryIo(data_, mutex_) {}

OwningInMemoryIo::OwningInMemoryIo(std::vector<char> data)
    : InMemoryIo(data_, mutex_), data_(std::move(data)) {}

OwningInMemoryIo::OwningInMemoryIo(std::string_view data)
    : InMemoryIo(data_, mutex_), data_(data.begin(), data.end()) {}

}  // namespace cuttlefish

//
// Copyright (C) 2025 The Android Open Source Project
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

#include <stddef.h>

#include <memory>
#include <string>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class LazilyLoadedFile {
 public:
  static Result<LazilyLoadedFile> Create(std::string filename, size_t size,
                                         std::unique_ptr<ReaderSeeker>);

  LazilyLoadedFile(LazilyLoadedFile&&);
  ~LazilyLoadedFile();
  LazilyLoadedFile& operator=(LazilyLoadedFile&&);

  Result<size_t> Read(char*, size_t);
  Result<void> Seek(size_t);

 private:
  struct Impl;

  LazilyLoadedFile(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace cuttlefish

/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <functional>
#include <memory>
#include <optional>

namespace cuttlefish {

class Lib {
 public:
  static std::optional<Lib> Load(const char* name);

  Lib() = default;

  Lib(const Lib&) = delete;
  Lib& operator=(const Lib&) = delete;

  Lib(Lib&&) = default;
  Lib& operator=(Lib&&) = default;

  using FunctionPtr = void (*)(void);

  FunctionPtr GetSymbol(const char* name);

 private:
  struct LibraryCloser {
   public:
    void operator()(void* library);
  };

  using ManagedLibrary = std::unique_ptr<void, LibraryCloser>;

  ManagedLibrary lib_;
};

}  // namespace cuttlefish

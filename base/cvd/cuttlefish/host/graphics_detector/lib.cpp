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

#include "cuttlefish/host/graphics_detector/lib.h"

#include <string>

#include <dlfcn.h>

namespace gfxstream {

void Lib::LibraryCloser::operator()(void* library) {
  if (library != nullptr) {
    dlclose(library);
  }
}

gfxstream::expected<Lib, std::string> Lib::Load(const char* name) {
  Lib lib;
  lib.lib_ = ManagedLibrary(dlopen(name, RTLD_NOW | RTLD_LOCAL));
  if (!lib.lib_) {
    return gfxstream::unexpected("Failed to load " + std::string(name));
  }
  return std::move(lib);
}

Lib::FunctionPtr Lib::GetSymbol(const char* name) {
  return reinterpret_cast<FunctionPtr>(dlsym(lib_.get(), name));
}

}  // namespace gfxstream

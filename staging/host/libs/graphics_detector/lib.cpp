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

#include "host/libs/graphics_detector/lib.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <dlfcn.h>

namespace cuttlefish {

void Lib::LibraryCloser::operator()(void* library) {
  if (library != nullptr) {
    dlclose(library);
  }
}

std::optional<Lib> Lib::Load(const char* name) {
  Lib lib;
  lib.lib_ = ManagedLibrary(dlopen(name, RTLD_NOW | RTLD_LOCAL));
  if (!lib.lib_) {
    LOG(ERROR) << "Failed to load library: " << name;
    return std::nullopt;
  }

  LOG(VERBOSE) << "Loaded library: " << name;
  return std::move(lib);
}

Lib::FunctionPtr Lib::GetSymbol(const char* name) {
  return reinterpret_cast<FunctionPtr>(dlsym(lib_.get(), name));
}

}  // namespace cuttlefish

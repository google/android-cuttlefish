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

#include <memory>

#include <android-base/logging.h>

#include <wayland-server-core.h>

namespace wayland {

// Helper for getting typed user data from of a wayland resource.
template <typename T>
T* GetUserData(wl_resource* resource) {
  void* data = wl_resource_get_user_data(resource);
  CHECK(data != nullptr);
  return static_cast<T*>(data);
}

// Helper for cleaning up typed user data from of a wayland resource.
template <typename T>
void DestroyUserData(wl_resource* resource) {
  std::unique_ptr<T> data(GetUserData<T>(resource));
  wl_resource_set_user_data(resource, nullptr);
}

}  // namespace wayland

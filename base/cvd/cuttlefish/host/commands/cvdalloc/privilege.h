/*
 * Copyright (C) 2025 The Android Open Source Project
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
#ifndef CUTTLEFISH_HOST_COMMANDS_CVDALLOC_PRIVILEGE_H_
#define CUTTLEFISH_HOST_COMMANDS_CVDALLOC_PRIVILEGE_H_

#include <unistd.h>

#include <optional>
#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

int BeginElevatedPrivileges();
int DropPrivileges(uid_t orig);
Result<void> ValidateCvdallocBinary(std::string_view path);

class ScopedPrivileges {
 public:
  static Result<ScopedPrivileges> Elevate();

  ScopedPrivileges(ScopedPrivileges&& other) noexcept;
  ScopedPrivileges& operator=(ScopedPrivileges&& other) = delete;
  ScopedPrivileges(const ScopedPrivileges&) = delete;
  ScopedPrivileges& operator=(const ScopedPrivileges&) = delete;
  ~ScopedPrivileges();

 private:
  explicit ScopedPrivileges(uid_t orig);

  std::optional<uid_t> orig_;
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_COMMANDS_CVDALLOC_PRIVILEGE_H_

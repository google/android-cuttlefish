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

#include "cuttlefish/host/libs/config/data_image_policy.h"

#include <string>

#include "absl/strings/string_view.h"

namespace cuttlefish {

namespace {

constexpr absl::string_view kAlwaysCreate = "always_create";
constexpr absl::string_view kResizeUpTo = "resize_up_to";
constexpr absl::string_view kUnknown = "unknown";
constexpr absl::string_view kUseExisting = "use_existing";

}  // namespace

DataImagePolicy DataImagePolicyFromString(absl::string_view policy) {
  if (policy == kAlwaysCreate) {
    return DataImagePolicy::AlwaysCreate;
  } else if (policy == kResizeUpTo) {
    return DataImagePolicy::ResizeUpTo;
  } else if (policy == kUseExisting) {
    return DataImagePolicy::UseExisting;
  } else {
    return DataImagePolicy::Unknown;
  }
}

std::string DataImagePolicyString(DataImagePolicy policy) {
  switch (policy) {
    case DataImagePolicy::AlwaysCreate:
      return std::string(kAlwaysCreate);
    case DataImagePolicy::ResizeUpTo:
      return std::string(kResizeUpTo);
    case DataImagePolicy::UseExisting:
      return std::string(kUseExisting);
    case DataImagePolicy::Unknown:
      return std::string(kUnknown);
  }
}

}  // namespace cuttlefish

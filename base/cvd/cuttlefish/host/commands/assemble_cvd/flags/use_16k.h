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

namespace cuttlefish {

// TODO: b/427589640 - vectorize this flag
class Use16kFlag {
 public:
  static Use16kFlag FromGlobalGflags();

  bool Use16k() const;

 private:
  explicit Use16kFlag(bool use_16k);

  bool use_16k_;
};

}  // namespace cuttlefish

//
// Copyright (C) 2022 The Android Open Source Project
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

#include <unistd.h>

#include <source_location>
#include <sstream>
#include <string>
#include <utility>

namespace cuttlefish {

class StackTraceEntry {
 public:
  StackTraceEntry(
      std::source_location location = std::source_location::current());

  StackTraceEntry(std::string expression, std::source_location location =
                                              std::source_location::current());

  StackTraceEntry(const StackTraceEntry& other);

  StackTraceEntry(StackTraceEntry&&) = default;
  StackTraceEntry& operator=(const StackTraceEntry& other);
  StackTraceEntry& operator=(StackTraceEntry&&) = default;

  template <typename T>
  StackTraceEntry& operator<<(T&& message_ext) & {
    message_ << std::forward<T>(message_ext);
    return *this;
  }
  template <typename T>
  StackTraceEntry operator<<(T&& message_ext) && {
    message_ << std::forward<T>(message_ext);
    return std::move(*this);
  }

  bool HasMessage() const;
  const std::string& Expression() const;
  const std::source_location& Location() const;
  std::string Message() const;

 private:
  std::string expression_;
  std::source_location location_;
  std::stringstream message_;
};

}  // namespace cuttlefish

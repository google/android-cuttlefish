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

#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "tl/expected.hpp"

#include "cuttlefish/ansi_codes/should_color.h"

namespace cuttlefish {

class StackTraceError;

class StackTraceEntry {
 public:
  StackTraceEntry(std::string file, size_t line, std::string pretty_function,
                  std::string function);

  StackTraceEntry(std::string file, size_t line, std::string pretty_function,
                  std::string function, std::string expression);

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

  operator StackTraceError() &&;
  template <typename T>
  operator tl::expected<T, StackTraceError>() &&;

  bool HasMessage() const;
  const std::string& Expression() const;
  const std::string& File() const;
  const std::string& Function() const;
  const std::string& PrettyFunction() const;
  size_t Line() const;
  std::string Message() const;

 private:
  std::string file_;
  size_t line_;
  std::string pretty_function_;
  std::string function_;
  std::string expression_;
  std::stringstream message_;
};

std::string ResultErrorFormat(bool color);

#define CF_STACK_TRACE_ENTRY(expression) \
  StackTraceEntry(__FILE__, __LINE__, __PRETTY_FUNCTION__, __func__, expression)

class StackTraceError {
 public:
  StackTraceError& PushEntry(StackTraceEntry entry) &;
  StackTraceError PushEntry(StackTraceEntry entry) &&;
  const std::vector<StackTraceEntry>& Stack() const;

  std::string Message() const;

  std::string Trace() const;

  std::string FormatForEnv(bool color = ShouldColorStdout()) const;

  template <typename T>
  operator tl::expected<T, StackTraceError>() && {
    return tl::unexpected(std::move(*this));
  }

 private:
  std::vector<StackTraceEntry> stack_;
};

inline StackTraceEntry::operator StackTraceError() && {
  return StackTraceError().PushEntry(std::move(*this));
}

template <typename T>
inline StackTraceEntry::operator tl::expected<T, StackTraceError>() && {
  return tl::unexpected(std::move(*this));
}

std::ostream& operator<<(std::ostream&, const StackTraceError&);

}  // namespace cuttlefish

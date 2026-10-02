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

template <typename E = void>
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

  operator StackTraceError<void>() &&;
  template <typename T>
  operator tl::expected<T, StackTraceError<void>>() &&;

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

template <typename E>
class StackTraceError {
 public:
  friend class StackTraceError<void>;
  using ValueOrBool = std::conditional_t<std::is_void_v<E>, bool, E>;

  explicit StackTraceError()
    requires std::is_void_v<E>
  {}
  explicit StackTraceError(StackTraceEntry first_entry)
    requires std::is_void_v<E>
  {
    stack_.emplace_back(std::move(first_entry));
  }
  explicit StackTraceError(std::vector<StackTraceEntry> stack)
    requires std::is_void_v<E>
      : stack_(std::move(stack)) {}
  explicit StackTraceError(ValueOrBool value)
    requires(!std::is_void_v<E>)
      : value_(std::move(value)) {}
  explicit StackTraceError(ValueOrBool value,
                           std::vector<StackTraceEntry> stack)
    requires(!std::is_void_v<E>)
      : value_(std::move(value)), stack_(std::move(stack)) {}
  explicit StackTraceError(ValueOrBool value, StackTraceEntry first_entry)
    requires(!std::is_void_v<E>)
      : value_(std::move(value)) {
    stack_.emplace_back(std::move(first_entry));
  }
  template <typename E2>
  explicit StackTraceError(StackTraceError<E2> other)
    requires std::is_void_v<E>
      : stack_(std::move(other.stack_)) {}

  StackTraceError<E>& PushEntry(StackTraceEntry entry) & {
    stack_.emplace_back(std::move(entry));
    return *this;
  }
  StackTraceError<E> PushEntry(StackTraceEntry entry) && {
    return std::move(this->PushEntry(entry));
  }
  const std::vector<StackTraceEntry>& Stack() const { return stack_; }

  std::string Message() const;

  std::string Trace() const;

  std::string FormatForEnv(bool color = ShouldColorStdout()) const;

  template <typename T>
  operator tl::expected<T, StackTraceError>() && {
    return tl::unexpected(std::move(*this));
  }

  ValueOrBool& Value()
    requires(!std::is_void_v<E>)
  {
    return value_;
  }

  const ValueOrBool& Value() const
    requires(!std::is_void_v<E>)
  {
    return value_;
  }

  ValueOrBool& operator*()
    requires(!std::is_void_v<E>)
  {
    return value_;
  }

  const ValueOrBool& operator*() const
    requires(!std::is_void_v<E>)
  {
    return value_;
  }

  template <typename E2>
  StackTraceError<E2> WithValue(E2 value) & {
    return StackTraceError<E2>(std::move(value), stack_);
  }

  template <typename E2>
  StackTraceError<E2> WithValue(E2 value) && {
    return StackTraceError<E2>(std::move(value), std::move(stack_));
  }

  operator StackTraceError<void>() & { return StackTraceError<void>(stack_); }

  operator StackTraceError<void>() && {
    return StackTraceError<void>(std::move(stack_));
  }

 private:
  ValueOrBool value_;
  std::vector<StackTraceEntry> stack_;
};

template <class E>
StackTraceError(E, StackTraceEntry) -> StackTraceError<E>;

inline StackTraceEntry::operator StackTraceError<void>() && {
  return StackTraceError().PushEntry(std::move(*this));
}

template <typename T>
inline StackTraceEntry::operator tl::expected<T, StackTraceError<void>>() && {
  return tl::unexpected(std::move(*this));
}

template <typename E>
std::ostream& operator<<(std::ostream& out, const StackTraceError<E>& error) {
  return out << StackTraceError<void>(error).FormatForEnv();
}

}  // namespace cuttlefish

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

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/result.h>  // IWYU pragma: export
#include <fmt/core.h>             // IWYU pragma: export

namespace cuttlefish {

class StackTraceError;

class StackTraceEntry {
 public:
  StackTraceEntry(std::string file, size_t line, std::string pretty_function)
      : file_(std::move(file)),
        line_(line),
        pretty_function_(std::move(pretty_function)) {}

  StackTraceEntry(std::string file, size_t line, std::string pretty_function,
                  std::string expression)
      : file_(std::move(file)),
        line_(line),
        pretty_function_(std::move(pretty_function)),
        expression_(std::move(expression)) {}

  StackTraceEntry(const StackTraceEntry& other)
      : file_(other.file_),
        line_(other.line_),
        pretty_function_(other.pretty_function_),
        expression_(other.expression_),
        message_(other.message_.str()) {}

  StackTraceEntry(StackTraceEntry&&) = default;
  StackTraceEntry& operator=(const StackTraceEntry& other) {
    file_ = other.file_;
    line_ = other.line_;
    pretty_function_ = other.pretty_function_;
    expression_ = other.expression_;
    message_.str(other.message_.str());
    return *this;
  }
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
  operator android::base::expected<T, StackTraceError>() &&;

  bool HasMessage() const { return !message_.str().empty(); }

  void Write(std::ostream& stream) const { stream << message_.str(); }
  void WriteVerbose(std::ostream& stream) const {
    auto str = message_.str();
    if (str.empty()) {
      stream << "Failure\n";
    } else {
      stream << message_.str() << "\n";
    }
    stream << " at " << file_ << ":" << line_ << "\n";
    stream << " in " << pretty_function_;
    if (!expression_.empty()) {
      stream << " for CF_EXPECT(" << expression_ << ")\n";
    }
  }

 private:
  std::string file_;
  size_t line_;
  std::string pretty_function_;
  std::string expression_;
  std::stringstream message_;
};

#define CF_STACK_TRACE_ENTRY(expression) \
  StackTraceEntry(__FILE__, __LINE__, __PRETTY_FUNCTION__, expression)

class StackTraceError {
 public:
  StackTraceError& PushEntry(StackTraceEntry entry) & {
    stack_.emplace_back(std::move(entry));
    return *this;
  }
  StackTraceError PushEntry(StackTraceEntry entry) && {
    stack_.emplace_back(std::move(entry));
    return std::move(*this);
  }
  const std::vector<StackTraceEntry>& Stack() const { return stack_; }

  std::string Message() const {
    std::stringstream writer;
    for (const auto& entry : stack_) {
      entry.Write(writer);
    }
    return writer.str();
  }

  std::string Trace() const {
    std::stringstream writer;
    for (const auto& entry : stack_) {
      entry.WriteVerbose(writer);
    }
    return writer.str();
  }

  template <typename T>
  operator android::base::expected<T, StackTraceError>() && {
    return android::base::unexpected(std::move(*this));
  }

 private:
  std::vector<StackTraceEntry> stack_;
};

inline StackTraceEntry::operator StackTraceError() && {
  return StackTraceError().PushEntry(std::move(*this));
}

template <typename T>
inline StackTraceEntry::operator android::base::expected<T,
                                                         StackTraceError>() && {
  return android::base::unexpected(std::move(*this));
}

template <typename T>
using Result = android::base::expected<T, StackTraceError>;

/**
 * Error return macro that includes the location in the file in the error
 * message. Use CF_ERRNO when including information from errno, otherwise use
 * the base CF_ERR macro.
 *
 * Example usage:
 *
 *     if (mkdir(path.c_str()) != 0) {
 *       return CF_ERRNO("mkdir(\"" << path << "\") failed: "
 *                       << strerror(errno));
 *     }
 *
 * This will return an error with the text
 *
 *     mkdir(...) failed: ...
 *       at path/to/file.cpp:50
 *       in Result<std::string> MyFunction()
 */
#define CF_ERR(MSG) (CF_STACK_TRACE_ENTRY("") << MSG)
#define CF_ERRNO(MSG) (CF_STACK_TRACE_ENTRY("") << MSG)
#define CF_ERRF(MSG, ...) \
  (CF_STACK_TRACE_ENTRY("") << fmt::format(FMT_STRING(MSG), __VA_ARGS__))

template <typename T>
T OutcomeDereference(std::optional<T>&& value) {
  return std::move(*value);
}

template <typename T>
typename std::conditional_t<std::is_void_v<T>, bool, T> OutcomeDereference(
    Result<T>&& result) {
  if constexpr (std::is_void<T>::value) {
    return result.ok();
  } else {
    return std::move(*result);
  }
}

template <typename T>
typename std::enable_if<std::is_convertible_v<T, bool>, T>::type
OutcomeDereference(T&& value) {
  return std::forward<T>(value);
}

inline bool TypeIsSuccess(bool value) { return value; }

template <typename T>
bool TypeIsSuccess(std::optional<T>& value) {
  return value.has_value();
}

template <typename T>
bool TypeIsSuccess(Result<T>& value) {
  return value.ok();
}

template <typename T>
bool TypeIsSuccess(Result<T>&& value) {
  return value.ok();
}

inline auto ErrorFromType(bool) { return StackTraceError(); }

template <typename T>
inline auto ErrorFromType(std::optional<T>) {
  return StackTraceError();
}

template <typename T>
auto ErrorFromType(Result<T>& value) {
  return value.error();
}

template <typename T>
auto ErrorFromType(Result<T>&& value) {
  return value.error();
}

#define CF_EXPECT_OVERLOAD(_1, _2, NAME, ...) NAME

#define CF_EXPECT2(RESULT, MSG)                               \
  ({                                                          \
    decltype(RESULT)&& macro_intermediate_result = RESULT;    \
    if (!TypeIsSuccess(macro_intermediate_result)) {          \
      auto current_entry = CF_STACK_TRACE_ENTRY(#RESULT);     \
      current_entry << MSG;                                   \
      auto error = ErrorFromType(macro_intermediate_result);  \
      error.PushEntry(std::move(current_entry));              \
      return error;                                           \
    };                                                        \
    OutcomeDereference(std::move(macro_intermediate_result)); \
  })

#define CF_EXPECT1(RESULT) CF_EXPECT2(RESULT, "")

/**
 * Error propagation macro that can be used as an expression.
 *
 * The first argument can be either a Result or a type that is convertible to
 * a boolean. A successful result will return the value inside the result, or
 * a conversion to a `true` value will return the unconverted value. This is
 * useful for e.g. pointers which can be tested through boolean conversion.
 *
 * In the failure case, this macro will return from the containing function
 * with a failing Result. The failing result will include information about the
 * call site, details from the optional second argument if given, and details
 * from the failing inner expression if it is a Result.
 *
 * This macro must be invoked only in functions that return a Result.
 *
 * Example usage:
 *
 *     Result<std::string> CreateTempDir();
 *
 *     Result<std::string> CreatePopulatedTempDir() {
 *       std::string dir = CF_EXPECT(CreateTempDir(), "Failed to create dir");
 *       // Do something with dir
 *       return dir;
 *     }
 *
 * If CreateTempDir fails, the function will returna Result with an error
 * message that looks like
 *
 *      Internal error
 *        at /path/to/otherfile.cpp:50
 *        in Result<std::string> CreateTempDir()
 *      Failed to create dir
 *        at /path/to/file.cpp:81:
 *        in Result<std::string> CreatePopulatedTempDir()
 *        for CF_EXPECT(CreateTempDir())
 */
#define CF_EXPECT(...) \
  CF_EXPECT_OVERLOAD(__VA_ARGS__, CF_EXPECT2, CF_EXPECT1)(__VA_ARGS__)

#define CF_EXPECTF(RESULT, MSG, ...) \
  CF_EXPECT(RESULT, fmt::format(FMT_STRING(MSG), __VA_ARGS__))

#define CF_COMPARE_EXPECT4(COMPARE_OP, LHS_RESULT, RHS_RESULT, MSG)         \
  ({                                                                        \
    auto&& lhs_macro_intermediate_result = LHS_RESULT;                      \
    auto&& rhs_macro_intermediate_result = RHS_RESULT;                      \
    bool comparison_result = lhs_macro_intermediate_result COMPARE_OP       \
        rhs_macro_intermediate_result;                                      \
    if (!comparison_result) {                                               \
      auto current_entry = CF_STACK_TRACE_ENTRY("");                        \
      current_entry << "Expected \"" << #LHS_RESULT << "\" " << #COMPARE_OP \
                    << " \"" << #RHS_RESULT << "\" but was "                \
                    << lhs_macro_intermediate_result << " vs "              \
                    << rhs_macro_intermediate_result << ".";                \
      current_entry << MSG;                                                 \
      auto error = ErrorFromType(false);                                    \
      error.PushEntry(std::move(current_entry));                            \
      return error;                                                         \
    };                                                                      \
    comparison_result;                                                      \
  })

#define CF_COMPARE_EXPECT3(COMPARE_OP, LHS_RESULT, RHS_RESULT) \
  CF_COMPARE_EXPECT4(COMPARE_OP, LHS_RESULT, RHS_RESULT, "")

#define CF_COMPARE_EXPECT_OVERLOAD(_1, _2, _3, _4, NAME, ...) NAME

#define CF_COMPARE_EXPECT(...)                                \
  CF_COMPARE_EXPECT_OVERLOAD(__VA_ARGS__, CF_COMPARE_EXPECT4, \
                             CF_COMPARE_EXPECT3)              \
  (__VA_ARGS__)

#define CF_EXPECT_EQ(LHS_RESULT, RHS_RESULT, ...) \
  CF_COMPARE_EXPECT(==, LHS_RESULT, RHS_RESULT, ##__VA_ARGS__)
#define CF_EXPECT_NE(LHS_RESULT, RHS_RESULT, ...) \
  CF_COMPARE_EXPECT(!=, LHS_RESULT, RHS_RESULT, ##__VA_ARGS__)
#define CF_EXPECT_LE(LHS_RESULT, RHS_RESULT, ...) \
  CF_COMPARE_EXPECT(<=, LHS_RESULT, RHS_RESULT, ##__VA_ARGS__)
#define CF_EXPECT_LT(LHS_RESULT, RHS_RESULT, ...) \
  CF_COMPARE_EXPECT(<, LHS_RESULT, RHS_RESULT, ##__VA_ARGS__)
#define CF_EXPECT_GE(LHS_RESULT, RHS_RESULT, ...) \
  CF_COMPARE_EXPECT(>=, LHS_RESULT, RHS_RESULT, ##__VA_ARGS__)
#define CF_EXPECT_GT(LHS_RESULT, RHS_RESULT, ...) \
  CF_COMPARE_EXPECT(>, LHS_RESULT, RHS_RESULT, ##__VA_ARGS__)

}  // namespace cuttlefish

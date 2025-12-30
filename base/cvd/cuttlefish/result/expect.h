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
#include <type_traits>
#include <utility>

#include <android-base/format.h>  // IWYU pragma: export
#include <android-base/result.h>  // IWYU pragma: export

#include "cuttlefish/result/error_type.h"
#include "cuttlefish/result/result_type.h"  // IWYU pragma: export

namespace cuttlefish {

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

inline void OutcomeDereference(Result<void>&&) {}

template <typename T>
T OutcomeDereference(Result<T>&& result) {
  return std::move(*result);
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

inline auto ErrorFromType(bool) { return StackTraceError(); }

template <typename T>
inline auto ErrorFromType(std::optional<T>) {
  return StackTraceError();
}

template <typename T>
auto ErrorFromType(Result<T>& value) {
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
      return std::move(error);                                \
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
                    << rhs_macro_intermediate_result << ". ";               \
      current_entry << MSG;                                                 \
      auto error = ErrorFromType(false);                                    \
      error.PushEntry(std::move(current_entry));                            \
      return std::move(error);                                              \
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

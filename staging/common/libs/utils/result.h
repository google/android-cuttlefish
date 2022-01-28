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

#include <type_traits>

#include <android-base/logging.h>
#include <android-base/result.h>

#include "common/libs/utils/files.h"

namespace cuttlefish {

using android::base::Result;

#define CF_ERR_MSG()                             \
  "  at " << __FILE__ << ":" << __LINE__ << "\n" \
          << "  in " << __PRETTY_FUNCTION__

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
#define CF_ERR(MSG) android::base::Error() << MSG << "\n" << CF_ERR_MSG()
#define CF_ERRNO(MSG)                        \
  android::base::ErrnoError() << MSG << "\n" \
                              << CF_ERR_MSG() << "\n  with errno " << errno

template <typename T>
typename std::conditional_t<std::is_void_v<T>, bool, T>
VoidSafeResultDereference(Result<T>&& result) {
  if constexpr (std::is_void<T>::value) {
    return result.ok();
  } else {
    return std::move(*result);
  }
}

template <typename T>
typename std::enable_if<std::is_convertible_v<T, bool>, T>::type
VoidSafeResultDereference(T&& value) {
  return std::forward<T>(value);
}

template <typename T>
bool IsOkOrTrue(T&& value) {
  if constexpr (std::is_convertible_v<T, bool>) {
    return (bool)value;
  } else {
    return value.ok();
  }
}

template <typename T>
auto ErrorFromBoolOrResult(T&& value) {
  if constexpr (std::is_convertible_v<T, bool>) {
    return (android::base::Error() << "Received `false`").str();
  } else {
    return value.error();
  }
}

#define CF_EXPECT_OVERLOAD(_1, _2, NAME, ...) NAME

#define CF_EXPECT2(RESULT, MSG)                                          \
  ({                                                                     \
    decltype(RESULT)&& macro_intermediate_result = RESULT;               \
    if (!IsOkOrTrue(macro_intermediate_result)) {                        \
      return android::base::Error()                                      \
             << ErrorFromBoolOrResult(macro_intermediate_result) << "\n" \
             << MSG << "\n"                                              \
             << CF_ERR_MSG() << "\n"                                     \
             << "  for CF_EXPECT(" << #RESULT << ")";                    \
    };                                                                   \
    VoidSafeResultDereference(std::move(macro_intermediate_result));     \
  })

#define CF_EXPECT1(RESULT) CF_EXPECT2(RESULT, "Received error")

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

}  // namespace cuttlefish

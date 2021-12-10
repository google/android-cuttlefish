/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Result<T, E> is the type that is used to pass a success value of type T or an error code of type
// E, optionally together with an error message. T and E can be any type. If E is omitted it
// defaults to int, which is useful when errno(3) is used as the error code.
//
// Passing a success value or an error value:
//
// Result<std::string> readFile() {
//   std::string content;
//   if (base::ReadFileToString("path", &content)) {
//     return content; // ok case
//   } else {
//     return ErrnoError() << "failed to read"; // error case
//   }
// }
//
// Checking the result and then unwrapping the value or propagating the error:
//
// Result<bool> hasAWord() {
//   auto content = readFile();
//   if (!content.ok()) {
//     return Error() << "failed to process: " << content.error();
//   }
//   return (*content.find("happy") != std::string::npos);
// }
//
// Using custom error code type:
//
// enum class MyError { A, B };
// struct MyErrorPrinter {
//   static std::string print(const MyError& e) {
//     switch(e) {
//       MyError::A: return "A";
//       MyError::B: return "B";
//     }
//   }
// };
//
// #define NewMyError(e) Error<MyError, MyErrorPrinter>(MyError::e)
//
// Result<T, MyError> val = NewMyError(A) << "some message";
//
// Formatting the error message using fmtlib:
//
// Errorf("{} errors", num); // equivalent to Error() << num << " errors";
// ErrnoErrorf("{} errors", num); // equivalent to ErrnoError() << num << " errors";
//
// Returning success or failure, but not the value:
//
// Result<void> doSomething() {
//   if (success) return {};
//   else return Error() << "error occurred";
// }
//
// Extracting error code:
//
// Result<T> val = Error(3) << "some error occurred";
// assert(3 == val.error().code());
//

#pragma once

#include <assert.h>
#include <errno.h>

#include <sstream>
#include <string>

#include "android-base/expected.h"
#include "android-base/format.h"

namespace android {
namespace base {

template <typename E = int>
struct ResultError {
  template <typename T>
  ResultError(T&& message, E code) : message_(std::forward<T>(message)), code_(code) {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator android::base::expected<T, ResultError<E>>() const {
    return android::base::unexpected(ResultError<E>(message_, code_));
  }

  std::string message() const { return message_; }
  E code() const { return code_; }

 private:
  std::string message_;
  E code_;
};

template <typename E>
inline bool operator==(const ResultError<E>& lhs, const ResultError<E>& rhs) {
  return lhs.message() == rhs.message() && lhs.code() == rhs.code();
}

template <typename E>
inline bool operator!=(const ResultError<E>& lhs, const ResultError<E>& rhs) {
  return !(lhs == rhs);
}

template <typename E>
inline std::ostream& operator<<(std::ostream& os, const ResultError<E>& t) {
  os << t.message();
  return os;
}

struct ErrnoPrinter {
  static std::string print(const int& e) { return strerror(e); }
};

template <typename E = int, typename ErrorCodePrinter = ErrnoPrinter>
class Error {
 public:
  Error() : code_(0), has_code_(false) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  Error(E code) : code_(code), has_code_(true) {}

  template <typename T, typename P, typename = std::enable_if_t<std::is_convertible_v<E, P>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator android::base::expected<T, ResultError<P>>() const {
    return android::base::unexpected(ResultError<P>(str(), static_cast<P>(code_)));
  }

  template <typename T>
  Error& operator<<(T&& t) {
    // NOLINTNEXTLINE(bugprone-suspicious-semicolon)
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, ResultError<E>>) {
      if (!has_code_) {
        code_ = t.code();
      }
      return (*this) << t.message();
    }
    int saved = errno;
    ss_ << t;
    errno = saved;
    return *this;
  }

  const std::string str() const {
    std::string str = ss_.str();
    if (has_code_) {
      if (str.empty()) {
        return ErrorCodePrinter::print(code_);
      }
      return std::move(str) + ": " + ErrorCodePrinter::print(code_);
    }
    return str;
  }

  Error(const Error&) = delete;
  Error(Error&&) = delete;
  Error& operator=(const Error&) = delete;
  Error& operator=(Error&&) = delete;

  template <typename T, typename... Args>
  friend Error ErrorfImpl(const T&& fmt, const Args&... args);

  template <typename T, typename... Args>
  friend Error ErrnoErrorfImpl(const T&& fmt, const Args&... args);

 private:
  Error(bool has_code, E code, const std::string& message) : code_(code), has_code_(has_code) {
    (*this) << message;
  }

  std::stringstream ss_;
  E code_;
  const bool has_code_;
};

inline Error<int, ErrnoPrinter> ErrnoError() {
  return Error<int, ErrnoPrinter>(errno);
}

template <typename E>
inline E ErrorCode(E code) {
  return code;
}

// Return the error code of the last ResultError object, if any.
// Otherwise, return `code` as it is.
template <typename T, typename E, typename... Args>
inline E ErrorCode(E code, T&& t, const Args&... args) {
  if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, ResultError<E>>) {
    return ErrorCode(t.code(), args...);
  }
  return ErrorCode(code, args...);
}

template <typename T, typename... Args>
inline Error<int, ErrnoPrinter> ErrorfImpl(const T&& fmt, const Args&... args) {
  return Error(false, ErrorCode(0, args...), fmt::format(fmt, args...));
}

template <typename T, typename... Args>
inline Error<int, ErrnoPrinter> ErrnoErrorfImpl(const T&& fmt, const Args&... args) {
  return Error<int, ErrnoPrinter>(true, errno, fmt::format(fmt, args...));
}

#define Errorf(fmt, ...) android::base::ErrorfImpl(FMT_STRING(fmt), ##__VA_ARGS__)
#define ErrnoErrorf(fmt, ...) android::base::ErrnoErrorfImpl(FMT_STRING(fmt), ##__VA_ARGS__)

template <typename T, typename E = int>
using Result = android::base::expected<T, ResultError<E>>;

// Macros for testing the results of functions that return android::base::Result.
// These also work with base::android::expected.
// For advanced matchers and customized error messages, see result-gtest.h.

#define CHECK_RESULT_OK(stmt)       \
  do {                              \
    const auto& tmp = (stmt);       \
    CHECK(tmp.ok()) << tmp.error(); \
  } while (0)

#define ASSERT_RESULT_OK(stmt)            \
  do {                                    \
    const auto& tmp = (stmt);             \
    ASSERT_TRUE(tmp.ok()) << tmp.error(); \
  } while (0)

#define EXPECT_RESULT_OK(stmt)            \
  do {                                    \
    auto tmp = (stmt);                    \
    EXPECT_TRUE(tmp.ok()) << tmp.error(); \
  } while (0)

}  // namespace base
}  // namespace android

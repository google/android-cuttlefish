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

#include <android-base/format.h>  // IWYU pragma: export
#include <android-base/logging.h>
#include <android-base/result.h>  // IWYU pragma: export

#if FMT_VERSION < 80000
namespace fmt {
  // fmt::runtime was added in v8.0.0
  template<typename T>
  const T& runtime(const T& param) {
    return param;
  }
}
#endif

namespace cuttlefish {

class StackTraceError;

class StackTraceEntry {
 public:
  enum class FormatSpecifier : char {
    /** Prefix multi-line output with an arrow. */
    kArrow = 'a',
    /** Use colors in all other output specifiers. */
    kColor = 'c',
    /** The function name without namespace or arguments. */
    kFunction = 'f',
    /** The CF_EXPECT(exp) expression. */
    kLongExpression = 'E',
    /** The source file path relative to ANDROID_BUILD_TOP and line number. */
    kLongLocation = 'L',
    /** The user-friendly string provided to CF_EXPECT. */
    kMessage = 'm',
    /** Prefix output with the stack frame index. */
    kNumbers = 'n',
    /** The function signature with fully-qualified types. */
    kPrettyFunction = 'F',
    /** The short location and short filename. */
    kShort = 's',
    /** The `exp` inside `CF_EXPECT(exp)` */
    kShortExpression = 'e',
    /** The source file basename and line number. */
    kShortLocation = 'l',
  };
  static constexpr auto kVerbose = {
      FormatSpecifier::kArrow,
      FormatSpecifier::kColor,
      FormatSpecifier::kNumbers,
      FormatSpecifier::kShort,
  };
  static constexpr auto kVeryVerbose = {
      FormatSpecifier::kArrow,          FormatSpecifier::kColor,
      FormatSpecifier::kNumbers,        FormatSpecifier::kLongLocation,
      FormatSpecifier::kPrettyFunction, FormatSpecifier::kLongExpression,
      FormatSpecifier::kMessage,
  };

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
  operator android::base::expected<T, StackTraceError>() &&;

  bool HasMessage() const;

  /*
   * Print a single stack trace entry out of a list of format specifiers.
   * Some format specifiers [a,c,n] cause changes that affect all lines, while
   * the rest amount to printing a single line in the output. This code is
   * reused by formatting code for both rendering individual stack trace
   * entries, and rendering an entire stack trace with multiple entries.
   */
  fmt::format_context::iterator format(
      fmt::format_context& ctx, const std::vector<FormatSpecifier>& specifiers,
      std::optional<int> index) const;

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

}  // namespace cuttlefish

/**
 * Specialized formatting for StackTraceEntry based on user-provided specifiers.
 *
 * A StackTraceEntry can be formatted with {:specifiers} in a `fmt::format`
 * string, where `specifiers` is an ordered list of characters deciding on the
 * format. `v` provides "verbose" output and `V` provides "very verbose" output.
 * See `StackTraceEntry::FormatSpecifiers` for more fine-grained specifiers.
 */
template <>
struct fmt::formatter<cuttlefish::StackTraceEntry> {
 public:
  constexpr auto parse(format_parse_context& ctx)
      -> format_parse_context::iterator {
    auto it = ctx.begin();
    while (it != ctx.end() && *it != '}') {
      if (*it == 'v') {
        for (const auto& specifier : cuttlefish::StackTraceEntry::kVerbose) {
          fmt_specs_.push_back(specifier);
        }
      } else if (*it == 'V') {
        for (const auto& specifier :
             cuttlefish::StackTraceEntry::kVeryVerbose) {
          fmt_specs_.push_back(specifier);
        }
      } else {
        fmt_specs_.push_back(
            static_cast<cuttlefish::StackTraceEntry::FormatSpecifier>(*it));
      }
      it++;
    }
    return it;
  }

  auto format(const cuttlefish::StackTraceEntry& entry,
              format_context& ctx) const -> format_context::iterator {
    return entry.format(ctx, fmt_specs_, std::nullopt);
  }

 private:
  std::vector<cuttlefish::StackTraceEntry::FormatSpecifier> fmt_specs_;
};

namespace cuttlefish {

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
    return fmt::format(fmt::runtime("{:m}"), *this);
  }

  std::string Trace() const { return fmt::format(fmt::runtime("{:v}"), *this); }

  std::string FormatForEnv(bool color = (isatty(STDERR_FILENO) == 1)) const {
    return fmt::format(fmt::runtime(ResultErrorFormat(color)), *this);
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

}  // namespace cuttlefish

/**
 * Specialized formatting for a collection of StackTraceEntry elements.
 *
 * Can be formatted by a `fmt::format` string as {:specifiers}. See
 * `fmt::formatter<cuttlefish::StackTraceEntry>` for the format specifiers of
 * individual entries. By default the specifier list is passed down to all
 * indivudal entries, with the following additional rules. The `^` specifier
 * will change the ordering from inner-to-outer instead of outer-to-inner, and
 * using the `/` specifier like `<abc>/<xyz>` will apply <xyz> only to the
 * innermost stack entry, and <abc> to all other stack entries.
 */
template <>
struct fmt::formatter<cuttlefish::StackTraceError> {
 public:
  constexpr auto parse(format_parse_context& ctx)
      -> format_parse_context::iterator {
    auto it = ctx.begin();
    while (it != ctx.end() && *it != '}') {
      if (*it == 'v') {
        for (const auto& spec : StackTraceEntry::kVerbose) {
          (has_inner_fmt_spec_ ? inner_fmt_specs_ : fmt_specs_).push_back(spec);
        }
      } else if (*it == 'V') {
        for (const auto& spec : StackTraceEntry::kVeryVerbose) {
          (has_inner_fmt_spec_ ? inner_fmt_specs_ : fmt_specs_).push_back(spec);
        }
      } else if (*it == '/') {
        has_inner_fmt_spec_ = true;
      } else if (*it == '^') {
        inner_to_outer_ = true;
      } else {
        (has_inner_fmt_spec_ ? inner_fmt_specs_ : fmt_specs_)
            .push_back(static_cast<StackTraceEntry::FormatSpecifier>(*it));
      }
      it++;
    }
    return it;
  }

  format_context::iterator format(const cuttlefish::StackTraceError& error,
                                  format_context& ctx) const;

 private:
  using StackTraceEntry = cuttlefish::StackTraceEntry;
  using StackTraceError = cuttlefish::StackTraceError;

  bool inner_to_outer_ = false;
  bool has_inner_fmt_spec_ = false;
  std::vector<StackTraceEntry::FormatSpecifier> fmt_specs_;
  std::vector<StackTraceEntry::FormatSpecifier> inner_fmt_specs_;
};

namespace cuttlefish {

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
                    << rhs_macro_intermediate_result << ". ";               \
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

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

std::ostream& operator<<(std::ostream&, const StackTraceError&);

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

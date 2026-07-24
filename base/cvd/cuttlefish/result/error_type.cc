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

#include "cuttlefish/result/error_type.h"

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "fmt/base.h"
#include "fmt/core.h"
#include "fmt/format.h"

#include "cuttlefish/ansi_codes/ansi_codes.h"

namespace cuttlefish {
namespace {

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

/*
 * Print a single stack trace entry out of a list of format specifiers.
 * Some format specifiers [a,c,n] cause changes that affect all lines, while
 * the rest amount to printing a single line in the output. This code is
 * reused by formatting code for both rendering individual stack trace
 * entries, and rendering an entire stack trace with multiple entries.
 */
fmt::format_context::iterator FormatEntry(
    fmt::format_context& ctx, const StackTraceEntry& entry,
    const std::vector<FormatSpecifier>& specifiers, std::optional<int> index) {
  auto out = ctx.out();
  std::vector<FormatSpecifier> filtered_specs;
  bool arrow = false;
  bool color = false;
  bool numbers = false;
  for (auto spec : specifiers) {
    switch (spec) {
      case FormatSpecifier::kArrow:
        arrow = true;
        continue;
      case FormatSpecifier::kColor:
        color = true;
        continue;
      case FormatSpecifier::kLongExpression:
      case FormatSpecifier::kShortExpression:
        if (entry.Expression().empty()) {
          continue;
        }
        break;
      case FormatSpecifier::kMessage:
        if (!entry.HasMessage()) {
          continue;
        }
        break;
      case FormatSpecifier::kNumbers:
        numbers = true;
        continue;
      default:  // fall through
        break;
    }
    filtered_specs.emplace_back(spec);
  }
  if (filtered_specs.empty()) {
    filtered_specs.push_back(FormatSpecifier::kShort);
  }
  for (size_t i = 0; i < filtered_specs.size(); i++) {
    if (index.has_value() && numbers) {
      if (color) {
        out = fmt::format_to(out, "{}. ", *index);
      } else {
        out = fmt::format_to(out, "{}. ", *index);
      }
    }
    if (numbers) {
      if (arrow && (int)i < ((int)filtered_specs.size()) - 2) {
        out = fmt::format_to(out, "|  ");
      } else if (arrow && i == filtered_specs.size() - 2) {
        out = fmt::format_to(out, "v  ");
      }
    } else {
      if (arrow && (int)i < ((int)filtered_specs.size()) - 2) {
        out = fmt::format_to(out, " | ");
      } else if (arrow && i == filtered_specs.size() - 2) {
        out = fmt::format_to(out, " v ");
      }
    }
    switch (filtered_specs[i]) {
      case FormatSpecifier::kFunction:
        out = fmt::format_to(out, "{}", entry.Function());
        break;
      case FormatSpecifier::kLongExpression:
        out = fmt::format_to(out, "CF_EXPECT({})", entry.Expression());
        break;
      case FormatSpecifier::kLongLocation:
        if (color) {
          out = fmt::format_to(out, "{}:{}", entry.File(), entry.Line());
        } else {
          out = fmt::format_to(out, "{}:{}", entry.File(), entry.Line());
        }
        break;
      case FormatSpecifier::kMessage:
        if (color) {
          out = fmt::format_to(out, "{}{}{}", kAnsiRed, entry.Message(),
                               kAnsiReset);
        } else {
          out = fmt::format_to(out, "{}", entry.Message());
        }
        break;
      case FormatSpecifier::kPrettyFunction:
        out = fmt::format_to(out, "{}", entry.PrettyFunction());
        break;
      case FormatSpecifier::kShort: {
        auto last_slash = entry.File().rfind("/");
        auto short_file = entry.File().substr(
            last_slash == std::string::npos ? 0 : last_slash + 1);
        std::string last;
        if (entry.HasMessage()) {
          last = color ? absl::StrCat(kAnsiRed, entry.Message(), kAnsiReset)
                       : entry.Message();
        }
        if (color) {
          out = fmt::format_to(out, "{}:{} | {} | {}", short_file, entry.Line(),
                               entry.Function(), last);
        } else {
          out = fmt::format_to(out, "{}:{} | {} | {}", short_file, entry.Line(),
                               entry.Function(), last);
        }
        break;
      }
      case FormatSpecifier::kShortExpression:
        out = fmt::format_to(out, "{}", entry.Expression());
        break;
      case FormatSpecifier::kShortLocation: {
        auto last_slash = entry.File().rfind("/");
        auto short_file = entry.File().substr(
            last_slash == std::string::npos ? 0 : last_slash + 1);
        if (color) {
          out = fmt::format_to(out, "{}:{}", short_file, entry.Line());
        } else {
          out = fmt::format_to(out, "{}:{}", short_file, entry.Line());
        }
        break;
      }
      default:
        fmt::format_to(out, "unknown specifier");
    }
    if (i < filtered_specs.size() - 1) {
      out = fmt::format_to(out, "\n");
    }
  }
  return out;
}

}  // namespace
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
        for (const auto& specifier : cuttlefish::kVerbose) {
          fmt_specs_.push_back(specifier);
        }
      } else if (*it == 'V') {
        for (const auto& specifier : cuttlefish::kVeryVerbose) {
          fmt_specs_.push_back(specifier);
        }
      } else {
        fmt_specs_.push_back(static_cast<cuttlefish::FormatSpecifier>(*it));
      }
      it++;
    }
    return it;
  }

  auto format(const cuttlefish::StackTraceEntry& entry,
              format_context& ctx) const -> format_context::iterator {
    return FormatEntry(ctx, entry, fmt_specs_, std::nullopt);
  }

 private:
  std::vector<cuttlefish::FormatSpecifier> fmt_specs_;
};

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
        for (const auto& spec : cuttlefish::kVerbose) {
          (has_inner_fmt_spec_ ? inner_fmt_specs_ : fmt_specs_).push_back(spec);
        }
      } else if (*it == 'V') {
        for (const auto& spec : cuttlefish::kVeryVerbose) {
          (has_inner_fmt_spec_ ? inner_fmt_specs_ : fmt_specs_).push_back(spec);
        }
      } else if (*it == '/') {
        has_inner_fmt_spec_ = true;
      } else if (*it == '^') {
        inner_to_outer_ = true;
      } else {
        (has_inner_fmt_spec_ ? inner_fmt_specs_ : fmt_specs_)
            .push_back(static_cast<cuttlefish::FormatSpecifier>(*it));
      }
      it++;
    }
    return it;
  }

  format_context::iterator format(const cuttlefish::StackTraceError& error,
                                  format_context& ctx) const {
    auto out = ctx.out();
    auto& stack = error.Stack();
    int begin = inner_to_outer_ ? 0 : stack.size() - 1;
    int end = inner_to_outer_ ? stack.size() : -1;
    int step = inner_to_outer_ ? 1 : -1;
    for (int i = begin; i != end; i += step) {
      auto& specs =
          has_inner_fmt_spec_ && i == 0 ? inner_fmt_specs_ : fmt_specs_;
      out = FormatEntry(ctx, stack[i], specs, i);
      if (i != end - step) {
        out = fmt::format_to(out, "\n");
      }
    }
    return out;
  }

 private:
  using StackTraceEntry = cuttlefish::StackTraceEntry;
  using StackTraceError = cuttlefish::StackTraceError;

  bool inner_to_outer_ = false;
  bool has_inner_fmt_spec_ = false;
  std::vector<cuttlefish::FormatSpecifier> fmt_specs_;
  std::vector<cuttlefish::FormatSpecifier> inner_fmt_specs_;
};

namespace cuttlefish {

StackTraceEntry::StackTraceEntry(std::string file, size_t line,
                                 std::string pretty_function,
                                 std::string function)
    : file_(std::move(file)),
      line_(line),
      pretty_function_(std::move(pretty_function)),
      function_(std::move(function)) {}

StackTraceEntry::StackTraceEntry(std::string file, size_t line,
                                 std::string pretty_function,
                                 std::string function, std::string expression)
    : file_(std::move(file)),
      line_(line),
      pretty_function_(std::move(pretty_function)),
      function_(std::move(function)),
      expression_(std::move(expression)) {}

StackTraceEntry::StackTraceEntry(const StackTraceEntry& other)
    : file_(other.file_),
      line_(other.line_),
      pretty_function_(other.pretty_function_),
      function_(other.function_),
      expression_(other.expression_),
      message_(other.message_.str()) {}

StackTraceEntry& StackTraceEntry::operator=(const StackTraceEntry& other) {
  file_ = other.file_;
  line_ = other.line_;
  pretty_function_ = other.pretty_function_;
  function_ = other.function_;
  expression_ = other.expression_;
  message_.str(other.message_.str());
  return *this;
}

bool StackTraceEntry::HasMessage() const { return !message_.str().empty(); }
const std::string& StackTraceEntry::Expression() const { return expression_; }
const std::string& StackTraceEntry::File() const { return file_; }
const std::string& StackTraceEntry::Function() const { return function_; }
const std::string& StackTraceEntry::PrettyFunction() const {
  return pretty_function_;
}
size_t StackTraceEntry::Line() const { return line_; }
std::string StackTraceEntry::Message() const { return message_.str(); }

StackTraceError& StackTraceError::PushEntry(StackTraceEntry entry) & {
  stack_.emplace_back(std::move(entry));
  return *this;
}

StackTraceError StackTraceError::PushEntry(StackTraceEntry entry) && {
  return std::move(this->PushEntry(entry));
}

const std::vector<StackTraceEntry>& StackTraceError::Stack() const {
  return stack_;
}

std::string StackTraceError::Message() const {
  return fmt::format(fmt::runtime("{:m}"), *this);
}

std::string StackTraceError::Trace() const {
  return fmt::format(fmt::runtime("{:v}"), *this);
}

std::string StackTraceError::FormatForEnv(bool color) const {
  return fmt::format(fmt::runtime(ResultErrorFormat(color)), *this);
}

std::string ResultErrorFormat(bool color) {
  auto error_format = getenv("CF_ERROR_FORMAT");
  std::string default_error_format = (color ? "cns/acLFEm" : "ns/aLFEm");
  std::string fmt_str =
      error_format == nullptr ? default_error_format : error_format;
  if (fmt_str.find("}") != std::string::npos) {
    fmt_str = "v";
  }
  return "{:" + fmt_str + "}";
}

std::ostream& operator<<(std::ostream& out, const StackTraceError& error) {
  return out << error.FormatForEnv();
}

}  // namespace cuttlefish

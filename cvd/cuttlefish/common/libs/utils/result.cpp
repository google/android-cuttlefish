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

#include "common/libs/utils/result.h"

#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <android-base/format.h>
#include <android-base/logging.h>
#include <android-base/result.h>

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

/*
 * Print a single stack trace entry out of a list of format specifiers.
 * Some format specifiers [a,c,n] cause changes that affect all lines, while
 * the rest amount to printing a single line in the output. This code is
 * reused by formatting code for both rendering individual stack trace
 * entries, and rendering an entire stack trace with multiple entries.
 */
fmt::format_context::iterator StackTraceEntry::format(
    fmt::format_context& ctx, const std::vector<FormatSpecifier>& specifiers,
    std::optional<int> index) const {
  static constexpr char kTerminalBoldRed[] = "\033[0;1;31m";
  static constexpr char kTerminalCyan[] = "\033[0;36m";
  static constexpr char kTerminalRed[] = "\033[0;31m";
  static constexpr char kTerminalReset[] = "\033[0m";
  static constexpr char kTerminalUnderline[] = "\033[0;4m";
  static constexpr char kTerminalYellow[] = "\033[0;33m";
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
        if (expression_.empty()) {
          continue;
        }
        break;
      case FormatSpecifier::kMessage:
        if (!HasMessage()) {
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
        out = fmt::format_to(out, "{}{}{}. ", kTerminalYellow, *index,
                             kTerminalReset);
      } else {
        out = fmt::format_to(out, "{}. ", *index);
      }
    }
    if (color) {
      out = fmt::format_to(out, "{}", kTerminalRed);
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
    if (color) {
      out = fmt::format_to(out, "{}", kTerminalReset);
    }
    switch (filtered_specs[i]) {
      case FormatSpecifier::kFunction:
        if (color) {
          out = fmt::format_to(out, "{}{}{}", kTerminalCyan, function_,
                               kTerminalReset);
        } else {
          out = fmt::format_to(out, "{}", function_);
        }
        break;
      case FormatSpecifier::kLongExpression:
        out = fmt::format_to(out, "CF_EXPECT({})", expression_);
        break;
      case FormatSpecifier::kLongLocation:
        if (color) {
          out = fmt::format_to(out, "{}{}{}:{}{}{}", kTerminalUnderline, file_,
                               kTerminalReset, kTerminalYellow, line_,
                               kTerminalYellow);
        } else {
          out = fmt::format_to(out, "{}:{}", file_, line_);
        }
        break;
      case FormatSpecifier::kMessage:
        if (color) {
          out = fmt::format_to(out, "{}{}{}", kTerminalBoldRed, message_.str(),
                               kTerminalReset);
        } else {
          out = fmt::format_to(out, "{}", message_.str());
        }
        break;
      case FormatSpecifier::kPrettyFunction:
        if (color) {
          out = fmt::format_to(out, "{}{}{}", kTerminalCyan, pretty_function_,
                               kTerminalReset);
        } else {
          out = fmt::format_to(out, "{}", pretty_function_);
        }
        break;
      case FormatSpecifier::kShort: {
        auto last_slash = file_.rfind("/");
        auto short_file =
            file_.substr(last_slash == std::string::npos ? 0 : last_slash + 1);
        std::string last;
        if (HasMessage()) {
          last = color ? kTerminalBoldRed + message_.str() + kTerminalReset
                       : message_.str();
        }
        if (color) {
          out = fmt::format_to(out, "{}{}{}:{}{}{} | {}{}{} | {}",
                               kTerminalUnderline, short_file, kTerminalReset,
                               kTerminalYellow, line_, kTerminalReset,
                               kTerminalCyan, function_, kTerminalReset, last);
        } else {
          out = fmt::format_to(out, "{}:{} | {} | {}", short_file, line_,
                               function_, last);
        }
        break;
      }
      case FormatSpecifier::kShortExpression:
        out = fmt::format_to(out, "{}", expression_);
        break;
      case FormatSpecifier::kShortLocation: {
        auto last_slash = file_.rfind("/");
        auto short_file =
            file_.substr(last_slash == std::string::npos ? 0 : last_slash + 1);
        if (color) {
          out = fmt::format_to(out, "{}{}{}:{}{}{}", kTerminalUnderline,
                               short_file, kTerminalReset, kTerminalYellow,
                               line_, kTerminalReset);
        } else {
          out = fmt::format_to(out, "{}:{}", short_file, line_);
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

}  // namespace cuttlefish

fmt::format_context::iterator
fmt::formatter<cuttlefish::StackTraceError>::format(
    const cuttlefish::StackTraceError& error, format_context& ctx) const {
  auto out = ctx.out();
  auto& stack = error.Stack();
  int begin = inner_to_outer_ ? 0 : stack.size() - 1;
  int end = inner_to_outer_ ? stack.size() : -1;
  int step = inner_to_outer_ ? 1 : -1;
  for (int i = begin; i != end; i += step) {
    auto& specs = has_inner_fmt_spec_ && i == 0 ? inner_fmt_specs_ : fmt_specs_;
    out = stack[i].format(ctx, specs, i);
    if (i != end - step) {
      out = fmt::format_to(out, "\n");
    }
  }
  return out;
}

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

#include "cuttlefish/result/stack_trace_entry.h"

#include <stddef.h>
#include <stdlib.h>

#include <sstream>
#include <string>
#include <utility>

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

}  // namespace cuttlefish

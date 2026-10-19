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

#include <source_location>
#include <string>
#include <utility>

namespace cuttlefish {

StackTraceEntry::StackTraceEntry(std::source_location location)
    : location_(std::move(location)) {}

StackTraceEntry::StackTraceEntry(std::string expression,
                                 std::source_location location)
    : expression_(std::move(expression)), location_(std::move(location)) {}

StackTraceEntry::StackTraceEntry(const StackTraceEntry& other)
    : expression_(other.expression_),
      location_(other.location_),
      message_(other.message_.str()) {}

StackTraceEntry& StackTraceEntry::operator=(const StackTraceEntry& other) {
  expression_ = other.expression_;
  location_ = other.location_;
  message_.str(other.message_.str());
  return *this;
}

bool StackTraceEntry::HasMessage() const { return !message_.str().empty(); }
const std::string& StackTraceEntry::Expression() const { return expression_; }
const std::source_location& StackTraceEntry::Location() const {
  return location_;
}
std::string StackTraceEntry::Message() const { return message_.str(); }

}  // namespace cuttlefish

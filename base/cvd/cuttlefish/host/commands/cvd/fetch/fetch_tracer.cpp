//
// Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/fetch/fetch_tracer.h"

#include <chrono>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

#include <fmt/format.h>

namespace cuttlefish {
namespace {

struct Phase {
  std::string name;
  std::chrono::seconds duration;
  std::optional<size_t> size_bytes;
};

}  // namespace

struct FetchTracer::TraceImpl {
  std::chrono::system_clock::time_point trace_start =
      std::chrono::system_clock::now();
  std::chrono::steady_clock::time_point phase_start =
      std::chrono::steady_clock::now();
  std::vector<Phase> phases;
};

namespace {

std::string FormatByteSize(uint64_t size) {
  if (size < 10240) {
    return fmt::format("{} B", size);
  }
  size /= 1024;
  if (size < 10240) {
    return fmt::format("{} KiB", size);
  }
  size /= 1024;
  if (size < 10240) {
    return fmt::format("{} MiB", size);
  }
  size /= 1024;
  if (size < 10240) {
    return fmt::format("{} GiB", size);
  }
  size /= 1024;
  return fmt::format("{} TiB", size);
}

std::string ToStyledString(const FetchTracer::TraceImpl& trace,
                           std::string indent_prefix) {
  std::stringstream ss;
  for (const Phase& phase : trace.phases) {
    ss << indent_prefix << phase.name << ": " << phase.duration.count() << "s";
    if (phase.size_bytes) {
      ss << ", " << FormatByteSize(*phase.size_bytes);
    }
    ss << '\n';
  }
  return ss.str();
}

}  // namespace

FetchTracer::Trace::Trace(FetchTracer::TraceImpl& impl) : impl_(impl) {}

void FetchTracer::Trace::CompletePhase(std::string phase_name,
                                       std::optional<size_t> size_bytes) {
  auto now = std::chrono::steady_clock::now();
  impl_.phases.push_back(Phase{
      std::move(phase_name),
      std::chrono::duration_cast<std::chrono::seconds>(now - impl_.phase_start),
      size_bytes});
  impl_.phase_start = now;
}

FetchTracer::Trace FetchTracer::NewTrace(std::string name) {
  std::lock_guard lock(traces_mtx_);
  auto& ref =
      traces_.emplace_back(std::move(name), std::make_shared<TraceImpl>());
  return Trace(*ref.second);
}

std::string FetchTracer::ToStyledString() const {
  std::stringstream ss;
  for (const auto& [name, trace] : traces_) {
    std::time_t start_time =
        std::chrono::system_clock::to_time_t(trace->trace_start);
    ss << name << ":\n";
    ss << " started: " << std::put_time(std::localtime(&start_time), "%F %T")
       << '\n';
    ss << cuttlefish::ToStyledString(*trace, " - ");
  }
  return ss.str();
}

}  // namespace cuttlefish

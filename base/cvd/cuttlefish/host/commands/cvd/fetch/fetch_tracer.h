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

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cuttlefish {

// FetchTracer allows tracking the performance of fetch operations.
// For each independing fetch, like fetching the host packages, a new trace
// should be created. Each trace is then split in phases, each of which tracks
// duration and, optionally, download size. The FetchTracer class is thread
// safe, the FetchTracer::Trace is not and each trace should only be used from a
// single thread.
class FetchTracer {
 public:
  struct TraceImpl;
  class Trace {
   public:
    explicit Trace(TraceImpl&);

    void CompletePhase(std::string phase_name,
                       std::optional<size_t> size = std::nullopt);

   private:
    TraceImpl& impl_;
  };

  Trace NewTrace(std::string name);

  std::string ToStyledString() const;

 private:
  std::vector<std::pair<std::string, std::shared_ptr<TraceImpl>>> traces_;
  std::mutex traces_mtx_;
};

}  // namespace cuttlefish

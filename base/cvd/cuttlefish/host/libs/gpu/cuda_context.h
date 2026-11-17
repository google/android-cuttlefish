/*
 * Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include <cuda.h>

#include <map>
#include <memory>
#include <mutex>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct CudaFunctions;
class ScopedCudaContext;

// Shared CUDA primary context for a GPU device. Thread-safe.
class CudaContext {
 public:
  // Returns nullptr if the device is not available.
  static std::shared_ptr<CudaContext> Get(int device_id = 0);

  // Pushes a CUDA context onto the calling thread's stack. Returns a
  // ScopedCudaContext that pops it on destruction. Safe to nest multiple
  // calls per thread; each push/pop is balanced by the destructor.
  static Result<ScopedCudaContext> Acquire(int device_id = 0);

  ~CudaContext();

  CUcontext get() const { return ctx_; }
  const CudaFunctions* cuda() const { return cuda_; }
  std::recursive_mutex& mutex() const { return mutex_; }

 private:
  CudaContext(CUcontext ctx, int device_id, const CudaFunctions* cuda);

  CUcontext ctx_;
  int device_id_;
  const CudaFunctions* cuda_;
  mutable std::recursive_mutex mutex_;
};

// RAII lock and push/pop for a CUDA context on the current thread.
class ScopedCudaContext {
 public:
  explicit ScopedCudaContext(const CudaContext& context);
  ScopedCudaContext(CUcontext ctx, const CudaFunctions* cuda,
                    std::recursive_mutex* mutex = nullptr);
  ~ScopedCudaContext();

  bool ok() const { return push_succeeded_; }

  ScopedCudaContext(const ScopedCudaContext&) = delete;
  ScopedCudaContext& operator=(const ScopedCudaContext&) = delete;
  ScopedCudaContext(ScopedCudaContext&& other);
  ScopedCudaContext& operator=(ScopedCudaContext&&) = delete;

 private:
  std::unique_lock<std::recursive_mutex> lock_;
  const CudaFunctions* cuda_ = nullptr;
  bool push_succeeded_ = false;
};

}  // namespace cuttlefish

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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_CUDA_CONTEXT_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_CUDA_CONTEXT_H_

#include <cuda.h>
#include <map>
#include <memory>
#include <mutex>

namespace cuttlefish {

// Manages a shared CUDA context for a GPU device.
//
// Uses CUDA's primary context mechanism to share a single context across
// multiple users (e.g., multiple encoder instances). The context is
// automatically retained when obtained and released on destruction.
//
// Thread safety: Get() is thread-safe. The returned context may be used
// from any thread, but must be pushed onto the thread's context stack
// using ScopedCudaContext before making CUDA API calls.
class CudaContext {
 public:
  // Gets the shared context for the given device.
  // Returns nullptr if the device is not available or CUDA initialization
  // fails.
  static std::shared_ptr<CudaContext> Get(int device_id = 0);

  ~CudaContext();

  // Returns the underlying CUDA context handle.
  CUcontext get() const { return ctx_; }

 private:
  CudaContext(CUcontext ctx, int device_id);

  CUcontext ctx_;
  int device_id_;
};

// RAII helper to push/pop a CUDA context on the current thread's stack.
//
// CUDA requires a context to be active on the calling thread before making
// API calls. This class pushes the context in its constructor and pops it
// in the destructor, ensuring balanced operations even if exceptions occur.
//
// Usage:
//   ScopedCudaContext scope(cuda_context);
//   if (!scope.ok()) {
//     // Handle error
//   }
//   // CUDA calls are now valid...
class ScopedCudaContext {
 public:
  explicit ScopedCudaContext(CUcontext ctx);
  ~ScopedCudaContext();

  // Returns true if the context was successfully pushed.
  bool ok() const { return push_succeeded_; }

  // Non-copyable, non-movable
  ScopedCudaContext(const ScopedCudaContext&) = delete;
  ScopedCudaContext& operator=(const ScopedCudaContext&) = delete;

 private:
  bool push_succeeded_ = false;
};

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_CUDA_CONTEXT_H_

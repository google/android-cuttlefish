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

#ifndef CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_CUDA_LOADER_H_
#define CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_CUDA_LOADER_H_

#include <cuda.h>

namespace cuttlefish {

// Function pointers for CUDA Driver API functions loaded at runtime via
// dlopen/dlsym. This allows the binary to run on machines without CUDA
// installed, falling back to software encoding.
//
// Versioned symbol names (e.g., cuCtxPushCurrent_v2) are used where
// required by the CUDA Driver ABI. See FFmpeg's dynlink_loader.h for
// the canonical reference.
struct CudaFunctions {
  CUresult (*cuInit)(unsigned int);
  CUresult (*cuDeviceGet)(CUdevice*, int);
  CUresult (*cuDeviceGetName)(char*, int, CUdevice);
  CUresult (*cuDevicePrimaryCtxRetain)(CUcontext*, CUdevice);
  CUresult (*cuDevicePrimaryCtxRelease)(CUdevice);
  CUresult (*cuCtxPushCurrent)(CUcontext);
  CUresult (*cuCtxPopCurrent)(CUcontext*);
  CUresult (*cuStreamCreate)(CUstream*, unsigned int);
  CUresult (*cuStreamDestroy)(CUstream);
  CUresult (*cuStreamSynchronize)(CUstream);
  CUresult (*cuMemAllocPitch)(CUdeviceptr*, size_t*, size_t, size_t,
                              unsigned int);
  CUresult (*cuMemFree)(CUdeviceptr);
  CUresult (*cuMemcpy2DAsync)(const CUDA_MEMCPY2D*, CUstream);
  CUresult (*cuGetErrorString)(CUresult, const char**);
};

// Attempts to load libcuda.so.1 and resolve all required function pointers.
//
// Returns a pointer to a static CudaFunctions struct on success, or nullptr
// if the library is not available or any required symbol is missing.
// Thread-safe (loads once via std::call_once). The returned pointer is valid
// for the lifetime of the process. The dlopen handle is intentionally leaked
// to avoid dlclose crashes with CUDA.
const CudaFunctions* TryLoadCuda();

}  // namespace cuttlefish

#endif  // CUTTLEFISH_HOST_FRONTEND_WEBRTC_LIBCOMMON_CUDA_LOADER_H_

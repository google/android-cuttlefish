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

#include "cuttlefish/result/result.h"

namespace cuttlefish {

// CUDA Driver API function pointers, loaded at runtime via dlopen.
// Versioned symbol names (e.g., cuCtxPushCurrent_v2) match the
// CUDA Driver ABI.
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

// Returns the CUDA function table. Thread-safe; loads once.
Result<const CudaFunctions*> TryLoadCuda();

}  // namespace cuttlefish

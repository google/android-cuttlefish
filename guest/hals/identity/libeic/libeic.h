/*
 * Copyright 2020, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HARDWARE_IDENTITY_LIBEIC_H
#define ANDROID_HARDWARE_IDENTITY_LIBEIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* The EIC_INSIDE_LIBEIC_H preprocessor symbol is used to enforce
 * library users to include only this file. All public interfaces, and
 * only public interfaces, must be included here.
 */
#define EIC_INSIDE_LIBEIC_H
#include "EicCbor.h"
#include "EicCommon.h"
#include "EicOps.h"
#include "EicPresentation.h"
#include "EicProvisioning.h"
#undef EIC_INSIDE_LIBEIC_H

#ifdef __cplusplus
}
#endif

#endif  // ANDROID_HARDWARE_IDENTITY_LIBEIC_H

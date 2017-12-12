/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <api_level_fixes.h>

#if GCE_PLATFORM_SDK_BEFORE(N)
extern "C" {
#endif
#include <dumpstate.h>
#if GCE_PLATFORM_SDK_BEFORE(N)
}
#endif

void dumpstate_board()
{
#if GCE_PLATFORM_SDK_AFTER(N_MR1)
    Dumpstate& ds = Dumpstate::GetInstance();

    ds.DumpFile("GCE INITIAL METADATA", "/initial.metadata");
#else
    dump_file("GCE INITIAL METADATA", "/initial.metadata");
#endif
};

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
#ifndef GCE_GET_PARTITION_NUM_H_
#define GCE_GET_PARTITION_NUM_H_

// Looks up the partition number for a given name.
// Path can be a full path to a partition number file. If null is provided the
// system default partition file is used.
// Returns -1 if the partition number cannot be found for any reason.
long GetPartitionNum(const char* name, const char* path = 0);

#endif  // GCE_GET_PARTITION_NUM_H_

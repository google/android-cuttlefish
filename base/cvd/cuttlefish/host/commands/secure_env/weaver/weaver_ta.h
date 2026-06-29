/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifdef __cplusplus
extern "C" {
#endif

// Main function for Rust implementation of Weaver.
// - fd_in: file descriptor for incoming serialized request messages
// - fd_out: file descriptor for outgoing serialized response messages
// - storage_path: path to the file used for persistent storage
// - snapshot_fd: file descriptor for a socket used to communicate with the
//   secure_env suspend-resume handler thread.
void weaver_ta_main(int fd_in, int fd_out, const char* storage_path,
                    int snapshot_fd);

#ifdef __cplusplus
}
#endif

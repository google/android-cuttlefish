/*
 * Copyright (C) 2022 The Android Open Source Project
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

// Main function for Rust implementation of KeyMint.
// - fd_in: file descriptor for incoming serialized request messages
// - fd_out: file descriptor for outgoing serialized response messages
// - security_level: security level to advertise; should be one of the integer
//   values from SecurityLevel.aidl.
// - trm: pointer to a valid `TpmResourceManager`, which must remain valid
//   for the entire duration of the function execution.
// - snapshot_socket_fd: file descriptor for a socket used to communicate with
//   the secure_env suspend-resume handler thread.
void kmr_ta_main(int fd_in, int fd_out, int security_level, void* trm,
                 int snapshot_socket_fd);

#ifdef __cplusplus
}
#endif

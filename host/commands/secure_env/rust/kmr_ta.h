#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Main function for Rust implementation of KeyMint.
// - fd_in: file descriptor for incoming serialized request messages
// - fd_out: file descriptor for outgoing serialized response messages
// - security_level: security level to advertize; should be one of the integer
//   values from SecurityLevel.aidl.
// - trm: pointer to a valid `TpmResourceManager`, which must remain valid
//   for the entire duration of the function execution.
void kmr_ta_main(int fd_in, int fd_out, int security_level, void* trm);

#ifdef __cplusplus
}
#endif

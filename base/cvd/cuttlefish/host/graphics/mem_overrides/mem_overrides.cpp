extern "C" {
#include "string/include/stringlib.h"
}

// b/277618912: glibc's aarch64 memcpy uses unaligned accesses which seems to
// cause SIGBUS errors on some Nvidia GPUs. Override memcpy here:

void* memcpy(void* dest, const void* src, size_t n) {
	return __memcpy_aarch64(dest, src, n);
}

void* memmove(void* dest, const void* src, size_t n) {
	return __memmove_aarch64(dest, src, n);
}

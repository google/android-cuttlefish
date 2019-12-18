#pragma once

#include <sys/types.h>

#ifdef NDEBUG
#define DEBUG_ONLY(x)
#else
#define DEBUG_ONLY(x)   x
#endif

void makeFdNonblocking(int fd);
void hexdump(const void *_data, size_t size);


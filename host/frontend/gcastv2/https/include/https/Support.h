#pragma once

#include <sys/types.h>

#include <string>

#ifdef NDEBUG
#define DEBUG_ONLY(x)
#else
#define DEBUG_ONLY(x)   x
#endif

void makeFdNonblocking(int fd);
void hexdump(const void *_data, size_t size);

void encodeBase64(const void *_data, size_t size, std::string *out);

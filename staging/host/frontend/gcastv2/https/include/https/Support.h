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

uint16_t U16_AT(const uint8_t *ptr);
uint32_t U32_AT(const uint8_t *ptr);
uint64_t U64_AT(const uint8_t *ptr);

uint16_t U16LE_AT(const uint8_t *ptr);
uint32_t U32LE_AT(const uint8_t *ptr);
uint64_t U64LE_AT(const uint8_t *ptr);

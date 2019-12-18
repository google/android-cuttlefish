#ifndef HEXDUMP_H_

#define HEXDUMP_H_

#include <sys/types.h>

#include <string>

namespace android {

void hexdump(
        const void *_data,
        size_t size,
        size_t indent = 0,
        std::string *appendTo = nullptr);

}  // namespace android

#endif  // HEXDUMP_H_

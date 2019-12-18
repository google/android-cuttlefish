#ifndef ANDROID_ERRORS_H_

#define ANDROID_ERRORS_H_

#include <errno.h>
#include <stdint.h>

namespace android {

typedef int32_t status_t;

enum {
    OK                = 0,
    UNKNOWN_ERROR     = -1,
    INVALID_OPERATION = -EINVAL,
    NO_INIT           = -ENODEV,
    ERROR_IO          = -EIO,
    ERROR_UNSUPPORTED = INVALID_OPERATION,
};

}  // namespace android

#endif  // ANDROID_ERRORS_H_

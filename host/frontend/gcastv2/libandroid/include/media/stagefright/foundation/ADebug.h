#ifndef A_DEBUG_H_

#define A_DEBUG_H_

#include <android-base/logging.h>
namespace android {

#define TRESPASS()      LOG(FATAL) << "Should not be here."

template<char prefix>
void Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("%c ", prefix);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

#ifdef LOG_NDEBUG
#define ALOGV   Log<'V'>
#else
#define ALOGV(...)
#endif

#define ALOGE   Log<'E'>
#define ALOGI   Log<'I'>
#define ALOGW   Log<'W'>

}  // namespace android

#endif  // A_DEBUG_H_


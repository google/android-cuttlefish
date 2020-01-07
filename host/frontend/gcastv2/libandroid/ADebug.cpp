
#include <media/stagefright/foundation/ADebug.h>

#include <stdio.h>
#include <stdlib.h>

#ifdef ANDROID
#include <cutils/log.h>
#endif

namespace android {

Logger::Logger(LogType type)
    : mLogType(type) {
    switch (mLogType) {
        case VERBOSE:
            mMessage = "V ";
            break;
        case INFO:
            mMessage = "I ";
            break;
        case WARNING:
            mMessage = "W ";
            break;
        case ERROR:
            mMessage = "E ";
            break;
        case FATAL:
            mMessage = "F ";
            break;

        default:
            break;
    }
}

Logger::~Logger() {
    mMessage.append("\n");

#if defined(TARGET_ANDROID_DEVICE)
    if (mLogType == VERBOSE) {
        return;
    }

    LOG_PRI(ANDROID_LOG_INFO, "ADebug", "%s", mMessage.c_str());
#else
    fprintf(stderr, "%s", mMessage.c_str());
    fflush(stderr);
#endif

    if (mLogType == FATAL) {
        abort();
    }
}

const char *LeafName(const char *s) {
    const char *lastSlash = strrchr(s, '/');
    return lastSlash != NULL ? lastSlash + 1 : s;
}

}  // namespace android

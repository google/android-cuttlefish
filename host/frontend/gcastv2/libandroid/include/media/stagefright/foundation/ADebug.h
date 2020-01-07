#ifndef A_DEBUG_H_

#define A_DEBUG_H_

#ifdef TARGET_ANDROID
#include <android-base/logging.h>
#else

#include "ABase.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <string_view>

enum LogType {
    VERBOSE,
    INFO,
    WARNING,
    ERROR,
    FATAL,
};

namespace android {

struct Logger {
    Logger(LogType type);
    virtual ~Logger();

    Logger &operator<<(std::string_view x) {
        mMessage.append(x);
        return *this;
    }

    Logger &operator<<(const std::string &x) {
        mMessage.append(x);
        return *this;
    }

    Logger &operator<<(const char *x) {
        mMessage.append(x);
        return *this;
    }

    Logger &operator<<(char *x) {
        mMessage.append(x);
        return *this;
    }

    template<class T> Logger &operator<<(const T &x) {
        mMessage.append(std::to_string(x));
        return *this;
    }

private:
    std::string mMessage;
    LogType mLogType;

    DISALLOW_EVIL_CONSTRUCTORS(Logger);
};

const char *LeafName(const char *s);

#undef LOG
#define LOG(type)                                                       \
    android::Logger(type)                                               \
        << android::LeafName(__FILE__) << ":" << __LINE__ << " "

#define CHECK(condition)                                \
    do {                                                \
        if (!(condition)) {                             \
            LOG(FATAL) << "CHECK(" #condition ") failed.";    \
        }                                               \
    } while (false)

using std::to_string;

inline std::string to_string(std::string_view s) {
    return std::string(s);
}

#define MAKE_COMPARATOR(suffix,op)                          \
    template<class A, class B>                              \
    std::string Compare_##suffix(const A &a, const B &b) {  \
        std::string res;                                    \
        if (!(a op b)) {                                    \
            res.append(to_string(a));                       \
            res.append(" vs. ");                            \
            res.append(to_string(b));                       \
        }                                                   \
        return res;                                         \
    }

MAKE_COMPARATOR(EQ,==)
MAKE_COMPARATOR(NE,!=)
MAKE_COMPARATOR(LE,<=)
MAKE_COMPARATOR(GE,>=)
MAKE_COMPARATOR(LT,<)
MAKE_COMPARATOR(GT,>)

#define CHECK_OP(x,y,suffix,op)                                         \
    do {                                                                \
        std::string ___res = android::Compare_##suffix(x, y);           \
        if (!___res.empty()) {                                          \
            LOG(FATAL) << "CHECK_" #suffix "(" #x "," #y ") failed: "   \
                       << ___res;                                       \
        }                                                               \
    } while (false)

#define CHECK_EQ(x,y)   CHECK_OP(x,y,EQ,==)
#define CHECK_NE(x,y)   CHECK_OP(x,y,NE,!=)
#define CHECK_LE(x,y)   CHECK_OP(x,y,LE,<=)
#define CHECK_LT(x,y)   CHECK_OP(x,y,LT,<)
#define CHECK_GE(x,y)   CHECK_OP(x,y,GE,>=)
#define CHECK_GT(x,y)   CHECK_OP(x,y,GT,>)

}  // namespace android

#endif  // defined(TARGET_ANDROID)

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


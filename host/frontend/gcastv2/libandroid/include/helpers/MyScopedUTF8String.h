#pragma once

#include <jni.h>

namespace android {

struct MyScopedUTF8String {
    explicit MyScopedUTF8String(JNIEnv *env, jstring stringObj);

    MyScopedUTF8String(const MyScopedUTF8String &) = delete;
    MyScopedUTF8String &operator=(const MyScopedUTF8String &) = delete;

    ~MyScopedUTF8String();

    const char *c_str() const;

private:
    JNIEnv *mEnv;
    jstring mStringObj;
    const char *mData;
};

}  // namespace android

#pragma once

#include <jni.h>

namespace android {

struct MyScopedByteArray {
    explicit MyScopedByteArray(JNIEnv *env, jbyteArray arrayObj);

    MyScopedByteArray(const MyScopedByteArray &) = delete;
    MyScopedByteArray &operator=(const MyScopedByteArray &) = delete;

    ~MyScopedByteArray();

    const jbyte *data() const;
    jsize size() const;

private:
    JNIEnv *mEnv;
    jbyteArray mArrayObj;
    jbyte *mElements;
    jsize mSize;
};

}  // namespace android


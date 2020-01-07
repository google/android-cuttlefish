#pragma once

#include <jni.h>
#include <sys/types.h>

namespace android {

void jniThrowException(
        JNIEnv *env, const char *className, const char *msg);

int jniRegisterNativeMethods(
        JNIEnv *env,
        const char *className,
        const JNINativeMethod *methods,
        size_t numMethods);

}  // namespace android


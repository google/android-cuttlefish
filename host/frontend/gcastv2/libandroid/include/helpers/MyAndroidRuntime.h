#pragma once

#include <jni.h>

namespace android {

struct MyAndroidRuntime {
    static void setJavaVM(JavaVM *vm);
    static JavaVM *getJavaVM();

    static JNIEnv *getJNIEnv();
};

}  // namespace android


#include <helpers/MyJNIHelpers.h>

#include <helpers/MyScopedLocalRef.h>
#include <media/stagefright/foundation/ADebug.h>

namespace android {

void jniThrowException(
        JNIEnv *env, const char *className, const char *msg) {
    MyScopedLocalRef<jclass> clazz(env, env->FindClass(className));
    CHECK(clazz.get() != nullptr);

    CHECK_EQ(env->ThrowNew(clazz.get(), msg), JNI_OK);
}

int jniRegisterNativeMethods(
        JNIEnv *env,
        const char *className,
        const JNINativeMethod *methods,
        size_t numMethods) {
    MyScopedLocalRef<jclass> clazz(env, env->FindClass(className));
    CHECK(clazz.get() != nullptr);

    CHECK_GE(env->RegisterNatives(clazz.get(), methods, numMethods), 0);

    return 0;
}

}  // namespaced android


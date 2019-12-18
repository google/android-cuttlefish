#include <helpers/MyAndroidRuntime.h>

namespace android {

static JavaVM *gVM;

// static
void MyAndroidRuntime::setJavaVM(JavaVM *vm) {
    gVM = vm;
}

// static
JavaVM *MyAndroidRuntime::getJavaVM() {
    return gVM;
}

// static
JNIEnv *MyAndroidRuntime::getJNIEnv() {
    JNIEnv *env;
    if (gVM->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        return nullptr;
    }

    return env;
}

}  // namespace android


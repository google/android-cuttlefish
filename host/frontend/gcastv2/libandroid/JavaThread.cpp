#include <helpers/JavaThread.h>

#include <helpers/MyAndroidRuntime.h>
#include <media/stagefright/foundation/ADebug.h>

namespace android {

void javaAttachThread() {
    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_4;
    args.name = (char *)"JavaThread";
    args.group = nullptr;

    JavaVM *vm = MyAndroidRuntime::getJavaVM();
    CHECK(vm);

    JNIEnv *env;
    jint result = vm->AttachCurrentThread(&env, (void *)&args);
    CHECK_EQ(result, JNI_OK);
}

void javaDetachThread() {
    JavaVM *vm = MyAndroidRuntime::getJavaVM();
    CHECK(vm);

    CHECK_EQ(vm->DetachCurrentThread(), JNI_OK);
}

}  // namespace android


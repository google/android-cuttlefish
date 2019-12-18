#include <helpers/MyScopedUTF8String.h>

namespace android {

MyScopedUTF8String::MyScopedUTF8String(JNIEnv *env, jstring stringObj)
    : mEnv(env),
      mStringObj(stringObj),
      mData(stringObj ? env->GetStringUTFChars(stringObj, nullptr) : nullptr) {
}

MyScopedUTF8String::~MyScopedUTF8String() {
    if (mData) {
        mEnv->ReleaseStringUTFChars(mStringObj, mData);
        mData = nullptr;
    }
}

const char *MyScopedUTF8String::c_str() const {
    return mData;
}

}  // namespace android

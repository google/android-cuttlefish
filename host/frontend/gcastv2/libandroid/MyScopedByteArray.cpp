#include <helpers/MyScopedByteArray.h>

namespace android {

MyScopedByteArray::MyScopedByteArray(JNIEnv *env, jbyteArray arrayObj)
    : mEnv(env),
      mArrayObj(arrayObj),
      mElements(nullptr),
      mSize(0) {
    if (mArrayObj) {
        mElements = env->GetByteArrayElements(mArrayObj, nullptr /* isCopy */);
        mSize = env->GetArrayLength(mArrayObj);
    }
}

MyScopedByteArray::~MyScopedByteArray() {
    if (mArrayObj) {
        mEnv->ReleaseByteArrayElements(mArrayObj, mElements, 0 /* mode */);
    }
}

const jbyte *MyScopedByteArray::data() const {
    return mElements;
}

jsize MyScopedByteArray::size() const {
    return mSize;
}

}  // namespace android


#include <source/StreamingSource.h>

namespace android {

StreamingSource::StreamingSource()
    : mCallbackFn(nullptr) {
}

void StreamingSource::setCallback(std::function<void(const std::shared_ptr<SBuffer> &)> cb) {
    CHECK(cb);
    mCallbackFn = cb;
}

void StreamingSource::onAccessUnit(const std::shared_ptr<SBuffer> &accessUnit) {
    if (mCallbackFn) {
        mCallbackFn(accessUnit);
    }
}

}  // namespace android

#include <source/StreamingSource.h>

#include <media/stagefright/foundation/ADebug.h>

namespace android {

StreamingSource::StreamingSource()
    : mCallbackFn(nullptr) {
}

void StreamingSource::setCallback(std::function<void(const std::shared_ptr<ABuffer> &)> cb) {
    CHECK(cb);
    mCallbackFn = cb;
}

void StreamingSource::onAccessUnit(const std::shared_ptr<ABuffer> &accessUnit) {
    if (mCallbackFn) {
        mCallbackFn(accessUnit);
    }
}

}  // namespace android

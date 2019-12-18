#include <source/StreamingSource.h>

#include <media/stagefright/foundation/ADebug.h>

namespace android {

StreamingSource::StreamingSource()
    : mNotify(nullptr),
      mCallbackFn(nullptr) {
}

void StreamingSource::setNotify(const sp<AMessage> &notify) {
    CHECK(!mCallbackFn);
    CHECK(notify);
    mNotify = notify;
}

void StreamingSource::setCallback(std::function<void(const sp<ABuffer> &)> cb) {
    CHECK(!mNotify);
    CHECK(cb);
    mCallbackFn = cb;
}

void StreamingSource::onAccessUnit(const sp<ABuffer> &accessUnit) {
    if (mCallbackFn) {
        mCallbackFn(accessUnit);
    } else {
        sp<AMessage> notify = mNotify->dup();
        notify->setBuffer("accessUnit", accessUnit);
        notify->post();
    }
}

}  // namespace android

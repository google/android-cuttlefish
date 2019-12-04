#include <source/StreamingSource.h>

#include <media/stagefright/foundation/ADebug.h>

namespace android {

StreamingSource::StreamingSource()
    : mNotify(nullptr),
      mCallbackFn(nullptr) {
}

void StreamingSource::setNotify(const std::shared_ptr<AMessage> &notify) {
    CHECK(!mCallbackFn);
    CHECK(notify);
    mNotify = notify;
}

void StreamingSource::setCallback(std::function<void(const std::shared_ptr<ABuffer> &)> cb) {
    CHECK(!mNotify);
    CHECK(cb);
    mCallbackFn = cb;
}

void StreamingSource::onAccessUnit(const std::shared_ptr<ABuffer> &accessUnit) {
    if (mCallbackFn) {
        mCallbackFn(accessUnit);
    } else {
        std::shared_ptr<AMessage> notify = mNotify->dup();
        notify->setBuffer("accessUnit", accessUnit);
        AMessage::post(notify);
    }
}

}  // namespace android

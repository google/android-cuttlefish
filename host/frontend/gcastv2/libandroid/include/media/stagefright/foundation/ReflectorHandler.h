#pragma once

#include <media/stagefright/foundation/AHandler.h>

namespace android {

template<class T>
struct ReflectorHandler : public AHandler {
    explicit ReflectorHandler(T *target)
        : mTarget(target) {
    }

    void onMessageReceived(const sp<AMessage> &msg) {
        mTarget->onMessageReceived(msg);
    }

private:
    T *mTarget;
};

}  // namespace android


#pragma once

#include <media/stagefright/foundation/ABuffer.h>

namespace android {

struct StreamingSink {
    explicit StreamingSink() = default;
    virtual ~StreamingSink() = default;

    virtual void onAccessUnit(const std::shared_ptr<ABuffer> &accessUnit) = 0;
};

}  // namespace android


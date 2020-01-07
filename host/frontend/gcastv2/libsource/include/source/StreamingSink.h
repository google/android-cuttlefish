#pragma once

#include <cinttypes>
#include <memory>

namespace android {

// TODO(jemoreira): add support for multitouch
struct InputEvent{
    InputEvent(int32_t down, int32_t x, int32_t y) : down(down), x(x), y(y) {}
    int32_t down;
    int32_t x;
    int32_t y;
};

struct StreamingSink {
    explicit StreamingSink() = default;
    virtual ~StreamingSink() = default;

    virtual void onAccessUnit(const std::shared_ptr<InputEvent> &accessUnit) = 0;
};

}  // namespace android


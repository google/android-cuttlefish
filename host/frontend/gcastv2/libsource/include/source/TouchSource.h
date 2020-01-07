#pragma once

#include <source/StreamingSource.h>

namespace android {

struct TouchSource : public StreamingSource {
    TouchSource() = default;

    status_t initCheck() const override;
    sp<AMessage> getFormat() const override;
    status_t start() override;
    status_t stop() override;
    status_t requestIDRFrame() override;

    void inject(bool down, int32_t x, int32_t y);
};

}  // namespace android

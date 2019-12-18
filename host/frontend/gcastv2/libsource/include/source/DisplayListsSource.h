#pragma once

#include <source/StreamingSource.h>

#include <media/stagefright/foundation/ABuffer.h>

namespace android {

struct DisplayListsSource : public StreamingSource {
    DisplayListsSource() = default;

    status_t initCheck() const override;
    sp<AMessage> getFormat() const override;
    status_t start() override;
    status_t stop() override;
    status_t requestIDRFrame() override;

    void inject(const void *data, size_t size);
};

}  // namespace android

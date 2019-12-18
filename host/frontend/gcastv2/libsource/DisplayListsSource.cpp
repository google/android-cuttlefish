#include <source/DisplayListsSource.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

status_t DisplayListsSource::initCheck() const {
    return OK;
}

sp<AMessage> DisplayListsSource::getFormat() const {
    return nullptr;
}

status_t DisplayListsSource::start() {
    return OK;
}

status_t DisplayListsSource::stop() {
    return OK;
}

status_t DisplayListsSource::requestIDRFrame() {
    return OK;
}

void DisplayListsSource::inject(const void *data, size_t size) {
    sp<ABuffer> accessUnit = new ABuffer(size);
    memcpy(accessUnit->data(), data, size);

    accessUnit->meta()->setInt64("timeUs", ALooper::GetNowUs());

    StreamingSource::onAccessUnit(accessUnit);
}

}  // namespace android


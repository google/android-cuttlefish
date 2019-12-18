#include <source/TouchSource.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

status_t TouchSource::initCheck() const {
    return OK;
}

sp<AMessage> TouchSource::getFormat() const {
    return nullptr;
}

status_t TouchSource::start() {
    return OK;
}

status_t TouchSource::stop() {
    return OK;
}

status_t TouchSource::requestIDRFrame() {
    return OK;
}

void TouchSource::inject(bool down, int32_t x, int32_t y) {
    sp<ABuffer> accessUnit = new ABuffer(3 * sizeof(int32_t));
    int32_t *data = reinterpret_cast<int32_t *>(accessUnit->data());
    data[0] = down;
    data[1] = x;
    data[2] = y;

    accessUnit->meta()->setInt64("timeUs", ALooper::GetNowUs());

    StreamingSource::onAccessUnit(accessUnit);
}

}  // namespace android


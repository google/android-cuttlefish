#include <source/CameraSource.h>

#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/avc_utils.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/TSPacketizer.h>

namespace android {

CameraSource::CameraSource()
    : mInitCheck(NO_INIT),
      mState(STOPPED),
      mSession(createCameraSession(&CameraSource::onFrameData, this)) {
    mInitCheck = OK;
}

CameraSource::~CameraSource() {
    stop();

    destroyCameraSession(mSession);
    mSession = nullptr;
}

status_t CameraSource::initCheck() const {
    return mInitCheck;
}

sp<AMessage> CameraSource::getFormat() const {
    return nullptr;
}

status_t CameraSource::start() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState != STOPPED) {
        return OK;
    }

    mState = RUNNING;
    startCameraSession(mSession);

    return OK;
}

status_t CameraSource::stop() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == STOPPED) {
        return OK;
    }

    mState = STOPPED;
    stopCameraSession(mSession);

    return OK;
}

status_t CameraSource::pause() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == PAUSED) {
        return OK;
    }

    if (mState != RUNNING) {
        return INVALID_OPERATION;
    }

    mState = PAUSED;
    pauseCameraSession(mSession);

    ALOGI("Now paused.");

    return OK;
}

status_t CameraSource::resume() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == RUNNING) {
        return OK;
    }

    if (mState != PAUSED) {
        return INVALID_OPERATION;
    }

    mState = RUNNING;
    resumeCameraSession(mSession);

    ALOGI("Now running.");

    return OK;
}

bool CameraSource::paused() const {
    return mState == PAUSED;
}

status_t CameraSource::requestIDRFrame() {
    return OK;
}

// static
void CameraSource::onFrameData(
        void *cookie,
        ssize_t csdIndex,
        int64_t timeUs,
        const void *data,
        size_t size) {
    return static_cast<CameraSource *>(cookie)->onFrameData(
            csdIndex, timeUs, data, size);
}

static uint32_t U32BE_AT(const void *_data) {
    const uint8_t *data = (const uint8_t *)_data;
    return data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
}

void CameraSource::onFrameData(
        ssize_t csdIndex, int64_t timeUs, const void *_data, size_t size) {
    const uint8_t *data = static_cast<const uint8_t *>(_data);

    ALOGV("got frame data csdIndex=%zd at %lld us, data %p, size %zu",
          csdIndex, timeUs, data, size);

    if (csdIndex >= 0) {
        sp<ABuffer> csd = new ABuffer(4 + size);
        memcpy(csd->data(), "\x00\x00\x00\x01", 4);
        memcpy(csd->data() + 4, data, size);

        mCSD.push_back(csd);
        return;
    }

    sp<ABuffer> accessUnit = new ABuffer(size);
    memcpy(accessUnit->data(), data, size);

    size_t offset = 0;
    while (offset + 3 < size) {
        uint32_t naluLength = U32BE_AT(&data[offset]);
        CHECK_LE(offset + 4 + naluLength, size);

        memcpy(accessUnit->data() + offset, "\x00\x00\x00\x01", 4);

        offset += 4;
        // ALOGI("nalType 0x%02x", data[offset] & 0x1f);

        CHECK_GT(naluLength, 0u);
        offset += naluLength;
    }
    CHECK_EQ(offset, size);

    if (IsIDR(accessUnit)) {
        accessUnit = prependCSD(accessUnit);
    }

    accessUnit->meta()->setInt64("timeUs", timeUs);

    sp<AMessage> notify = mNotify->dup();
    notify->setBuffer("accessUnit", accessUnit);
    notify->post();
}

sp<ABuffer> CameraSource::prependCSD(const sp<ABuffer> &accessUnit) const {
    size_t size = 0;
    for (const auto &csd : mCSD) {
        size += csd->size();
    }

    sp<ABuffer> dup = new ABuffer(accessUnit->size() + size);
    size_t offset = 0;
    for (const auto &csd : mCSD) {
        memcpy(dup->data() + offset, csd->data(), csd->size());
        offset += csd->size();
    }

    memcpy(dup->data() + offset, accessUnit->data(), accessUnit->size());

    return dup;
}

}  // namespace android

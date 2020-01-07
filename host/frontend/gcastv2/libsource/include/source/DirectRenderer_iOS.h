#pragma once

#include <VideoToolbox/VideoToolbox.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

struct DirectRenderer_iOS {
    DirectRenderer_iOS();
    ~DirectRenderer_iOS();

    void setFormat(size_t index, const sp<AMessage> &format);
    void queueAccessUnit(size_t index, const sp<ABuffer> &accessUnit);

    void render(CVImageBufferRef imageBuffer);

private:
    CMVideoFormatDescriptionRef mVideoFormatDescription;
    VTDecompressionSessionRef mSession;

    DISALLOW_EVIL_CONSTRUCTORS(DirectRenderer_iOS);
};

}  // namespace android


#ifndef ANDROID_MEDIA_BUFFER_H_

#define ANDROID_MEDIA_BUFFER_H_

#include <media/stagefright/MetaData.h>

namespace android {

struct ABuffer;

struct MediaBuffer {
    MediaBuffer(size_t size)
        : mBuffer(new ABuffer(size)) {
    }

    std::shared_ptr<MetaData> meta_data() {
        if (!mMeta) {
            mMeta.reset(new MetaData);
        }

        return mMeta;
    }

    void *data() {
        return mBuffer->data();
    }

private:
    std::shared_ptr<ABuffer> mBuffer;
    std::shared_ptr<MetaData> mMeta;

    DISALLOW_EVIL_CONSTRUCTORS(MediaBuffer);
};

}  // namespace android

#endif  // ANDROID_MEDIA_BUFFER_H_

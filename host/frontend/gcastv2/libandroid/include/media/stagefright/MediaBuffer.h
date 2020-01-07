#ifndef ANDROID_MEDIA_BUFFER_H_

#define ANDROID_MEDIA_BUFFER_H_

#include <media/stagefright/MetaData.h>

namespace android {

struct ABuffer;

struct MediaBuffer {
    MediaBuffer(size_t size)
        : mBuffer(new ABuffer(size)) {
    }

    sp<MetaData> meta_data() {
        if (mMeta == NULL) {
            mMeta = new MetaData;
        }

        return mMeta;
    }

    void *data() {
        return mBuffer->data();
    }

private:
    sp<ABuffer> mBuffer;
    sp<MetaData> mMeta;

    DISALLOW_EVIL_CONSTRUCTORS(MediaBuffer);
};

}  // namespace android

#endif  // ANDROID_MEDIA_BUFFER_H_

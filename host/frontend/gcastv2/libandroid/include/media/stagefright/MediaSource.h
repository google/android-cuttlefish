#ifndef ANDROID_MEDIASOURCE_H_

#define ANDROID_MEDIASOURCE_H_

#include <utils/Errors.h>
#include <utils/RefBase.h>

namespace android {

struct MediaBuffer;
struct MetaData;

struct MediaSource : public RefBase {
    struct ReadOptions {
    };

    MediaSource() {}

    virtual status_t start(MetaData *params = NULL) = 0;
    virtual status_t stop() = 0;
    virtual sp<MetaData> getFormat() = 0;

    virtual status_t read(
            MediaBuffer **out, const ReadOptions *params = NULL) = 0;

protected:
    virtual ~MediaSource() {}

private:
    DISALLOW_EVIL_CONSTRUCTORS(MediaSource);
};

}  // namespace android

#endif  // ANDROID_MEDIASOURCE_H_

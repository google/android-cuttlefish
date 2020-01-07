#ifndef ANDROID_MEDIASOURCE_H_

#define ANDROID_MEDIASOURCE_H_

#include <utils/Errors.h>

#include <memory>

namespace android {

struct MediaBuffer;
struct MetaData;

struct MediaSource {
    struct ReadOptions {
    };

    MediaSource() {}

    virtual status_t start(MetaData *params = NULL) = 0;
    virtual status_t stop() = 0;
    virtual std::shared_ptr<MetaData> getFormat() = 0;

    virtual status_t read(
            MediaBuffer **out, const ReadOptions *params = NULL) = 0;

    virtual ~MediaSource() {}

private:
    DISALLOW_EVIL_CONSTRUCTORS(MediaSource);
};

}  // namespace android

#endif  // ANDROID_MEDIASOURCE_H_

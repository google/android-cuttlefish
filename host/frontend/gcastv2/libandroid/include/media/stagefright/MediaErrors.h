#ifndef ANDROID_MEDIA_ERRORS_H_

#define ANDROID_MEDIA_ERRORS_H_

namespace android {

enum {
    ERROR_END_OF_STREAM = -10000,
    ERROR_MALFORMED,
    INFO_DISCONTINUITY,
};

}  // namespace android

#endif  // ANDROID_MEDIA_ERRORS_H_

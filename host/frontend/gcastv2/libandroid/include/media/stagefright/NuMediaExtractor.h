#ifndef NU_MEDIA_EXTRACTOR_H_

#define NU_MEDIA_EXTRACTOR_H_

#include <stdio.h>

#include <utils/Errors.h>
#include <utils/RefBase.h>

namespace android {

struct ABuffer;
struct AMessage;
struct AnotherPacketSource;
struct ATSParser;

struct NuMediaExtractor : public RefBase {
    NuMediaExtractor();

    status_t setDataSource(const char *path);

    size_t countTracks() const;
    status_t getTrackFormat(size_t index, sp<AMessage> *format) const;

    status_t selectTrack(size_t index);

    status_t getSampleTime(int64_t *timeUs);
    status_t getSampleTrackIndex(size_t *index);
    status_t readSampleData(sp<ABuffer> accessUnit);

    status_t advance();

protected:
    virtual ~NuMediaExtractor();

private:
    enum Flags {
        FLAG_AUDIO_SELECTED = 1,
        FLAG_VIDEO_SELECTED = 2,
    };

    uint32_t mFlags;
    sp<ATSParser> mParser;
    FILE *mFile;

    sp<AnotherPacketSource> mAudioSource;
    sp<AnotherPacketSource> mVideoSource;
    size_t mNumTracks;
    ssize_t mAudioTrackIndex;
    ssize_t mVideoTrackIndex;

    sp<ABuffer> mNextBuffer[2];
    status_t mFinalResult[2];
    ssize_t mNextIndex;

    status_t feedMoreData();
    void fetchSamples();

    DISALLOW_EVIL_CONSTRUCTORS(NuMediaExtractor);
};

}  // namespace android

#endif // NU_MEDIA_EXTRACTOR_H_

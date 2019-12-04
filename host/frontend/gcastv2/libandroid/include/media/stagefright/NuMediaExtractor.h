#ifndef NU_MEDIA_EXTRACTOR_H_

#define NU_MEDIA_EXTRACTOR_H_

#include <stdio.h>

#include <memory>

#include <utils/Errors.h>
#include <media/stagefright/foundation/ABase.h>

namespace android {

struct ABuffer;
struct AMessage;
struct AnotherPacketSource;
struct ATSParser;

struct NuMediaExtractor {
    NuMediaExtractor();

    status_t setDataSource(const char *path);

    size_t countTracks() const;
    status_t getTrackFormat(size_t index, std::shared_ptr<AMessage> *format) const;

    status_t selectTrack(size_t index);

    status_t getSampleTime(int64_t *timeUs);
    status_t getSampleTrackIndex(size_t *index);
    status_t readSampleData(std::shared_ptr<ABuffer> accessUnit);

    status_t advance();

protected:
    virtual ~NuMediaExtractor();

private:
    enum Flags {
        FLAG_AUDIO_SELECTED = 1,
        FLAG_VIDEO_SELECTED = 2,
    };

    uint32_t mFlags;
    std::shared_ptr<ATSParser> mParser;
    FILE *mFile;

    std::shared_ptr<AnotherPacketSource> mAudioSource;
    std::shared_ptr<AnotherPacketSource> mVideoSource;
    size_t mNumTracks;
    ssize_t mAudioTrackIndex;
    ssize_t mVideoTrackIndex;

    std::shared_ptr<ABuffer> mNextBuffer[2];
    status_t mFinalResult[2];
    ssize_t mNextIndex;

    status_t feedMoreData();
    void fetchSamples();

    DISALLOW_EVIL_CONSTRUCTORS(NuMediaExtractor);
};

}  // namespace android

#endif // NU_MEDIA_EXTRACTOR_H_

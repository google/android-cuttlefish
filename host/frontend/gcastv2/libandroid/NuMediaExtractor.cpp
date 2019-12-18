#include <media/stagefright/NuMediaExtractor.h>

#include <media/stagefright/AnotherPacketSource.h>
#include <media/stagefright/ATSParser.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

namespace android {

NuMediaExtractor::NuMediaExtractor()
    : mFlags(0),
      mParser(new ATSParser),
      mFile(NULL),
      mNumTracks(0),
      mAudioTrackIndex(-1),
      mVideoTrackIndex(-1),
      mNextIndex(-1) {
    mFinalResult[0] = mFinalResult[1] = OK;
}

NuMediaExtractor::~NuMediaExtractor() {
    if (mFile) {
        fclose(mFile);
        mFile = NULL;
    }
}

status_t NuMediaExtractor::setDataSource(const char *path) {
    if (mFile) {
        return UNKNOWN_ERROR;
    }

    mFile = fopen(path, "rb");

    if (!mFile) {
        return -ENOENT;
    }

    for (size_t i = 0; i < 1024; ++i) {
        if (mVideoSource == NULL) {
            mVideoSource = mParser->getSource(ATSParser::VIDEO);
        }

        if (mAudioSource == NULL) {
            mAudioSource = mParser->getSource(ATSParser::AUDIO);
        }

        if (feedMoreData() != OK) {
            break;
        }
    }

    if (mAudioSource != NULL && mAudioSource->getFormat() == NULL) {
        mAudioSource.clear();
    }

    if (mVideoSource != NULL && mVideoSource->getFormat() == NULL) {
        mVideoSource.clear();
    }

    mNumTracks = 0;
    if (mAudioSource != NULL) {
        mAudioTrackIndex = mNumTracks;
        ++mNumTracks;
    }
    if (mVideoSource != NULL) {
        mVideoTrackIndex = mNumTracks;
        ++mNumTracks;
    }

    return OK;
}

size_t NuMediaExtractor::countTracks() const {
    return mNumTracks;
}

status_t NuMediaExtractor::getTrackFormat(
        size_t index, sp<AMessage> *format) const {
    CHECK_LT(index, mNumTracks);

    sp<MetaData> meta;
    if ((ssize_t)index == mAudioTrackIndex) {
        meta = mAudioSource->getFormat();
    } else {
        meta = mVideoSource->getFormat();
    }

    return convertMetaDataToMessage(meta, format);
}

status_t NuMediaExtractor::selectTrack(size_t index) {
    CHECK_LT(index, mNumTracks);

    if ((ssize_t)index == mAudioTrackIndex) {
        mFlags |= FLAG_AUDIO_SELECTED;
    } else {
        mFlags |= FLAG_VIDEO_SELECTED;
    }

    return OK;
}

status_t NuMediaExtractor::getSampleTime(int64_t *timeUs) {
    fetchSamples();

    if (mNextIndex < 0) {
        return ERROR_END_OF_STREAM;
    }

    CHECK(mNextBuffer[mNextIndex]->meta()->findInt64("timeUs", timeUs));

    return OK;
}

status_t NuMediaExtractor::getSampleTrackIndex(size_t *index) {
    fetchSamples();

    if (mNextIndex < 0) {
        return ERROR_END_OF_STREAM;
    }

    *index = mNextIndex;

    return OK;
}

status_t NuMediaExtractor::readSampleData(sp<ABuffer> accessUnit) {
    fetchSamples();

    if (mNextIndex < 0) {
        return ERROR_END_OF_STREAM;
    }

    accessUnit->setRange(0, mNextBuffer[mNextIndex]->size());

    memcpy(accessUnit->data(),
           mNextBuffer[mNextIndex]->data(),
           mNextBuffer[mNextIndex]->size());

    return OK;
}

status_t NuMediaExtractor::advance() {
    if (mNextIndex < 0) {
        return ERROR_END_OF_STREAM;
    }

    mNextBuffer[mNextIndex].clear();
    mNextIndex = -1;

    return OK;
}

void NuMediaExtractor::fetchSamples() {
    status_t err;

    if ((mFlags & FLAG_AUDIO_SELECTED)
            && mNextBuffer[mAudioTrackIndex] == NULL
            && mFinalResult[mAudioTrackIndex] == OK) {
        status_t finalResult;
        while (!mAudioSource->hasBufferAvailable(&finalResult)
                && finalResult == OK) {
            feedMoreData();
        }

        err = mAudioSource->dequeueAccessUnit(&mNextBuffer[mAudioTrackIndex]);

        if (err != OK) {
            mFinalResult[mAudioTrackIndex] = err;
        }
    }

    if ((mFlags & FLAG_VIDEO_SELECTED)
            && mNextBuffer[mVideoTrackIndex] == NULL
            && mFinalResult[mVideoTrackIndex] == OK) {
        status_t finalResult;
        while (!mVideoSource->hasBufferAvailable(&finalResult)
                && finalResult == OK) {
            feedMoreData();
        }

        err = mVideoSource->dequeueAccessUnit(&mNextBuffer[mVideoTrackIndex]);

        if (err != OK) {
            mFinalResult[mVideoTrackIndex] = err;
        }
    }

    bool haveValidTime = false;
    int64_t minTimeUs = -1ll;
    size_t minIndex = 0;

    if ((mFlags & FLAG_AUDIO_SELECTED)
            && mNextBuffer[mAudioTrackIndex] != NULL) {
        CHECK(mNextBuffer[mAudioTrackIndex]->meta()->findInt64("timeUs", &minTimeUs));
        haveValidTime = true;
        minIndex = mAudioTrackIndex;
    }

    if ((mFlags & FLAG_VIDEO_SELECTED)
            && mNextBuffer[mVideoTrackIndex] != NULL) {
        int64_t timeUs;
        CHECK(mNextBuffer[mVideoTrackIndex]->meta()->findInt64("timeUs", &timeUs));

        if (!haveValidTime || timeUs < minTimeUs) {
            haveValidTime = true;
            minTimeUs = timeUs;
            minIndex = mVideoTrackIndex;
        }
    }

    if (!haveValidTime) {
        mNextIndex = -1;
    } else {
        mNextIndex = minIndex;
    }
}

status_t NuMediaExtractor::feedMoreData() {
    uint8_t packet[188];
    ssize_t n = fread(packet, 1, sizeof(packet), mFile);

    status_t err;

    if (n < (ssize_t)sizeof(packet)) {
        err = UNKNOWN_ERROR;
    } else {
        err = mParser->feedTSPacket(packet, sizeof(packet));
    }

    if (err != OK) {
        mParser->signalEOS(err);
        return err;
    }

    return OK;
}

}  // namespace android

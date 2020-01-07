#include <source/AudioSource.h>

#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/TSPacketizer.h>
#include <media/stagefright/foundation/hexdump.h>
#include <libyuv/convert.h>

#include <system/audio.h>

#include "host/libs/config/cuttlefish_config.h"

#include <aacenc_lib.h>

#include <opus.h>

#include <gflags/gflags.h>
#include <cmath>

#define LOG_AUDIO       0

namespace android {

namespace {

// These definitions are deleted in master, copying here temporarily
typedef uint32_t size32_t;

struct timespec32 {
  uint32_t tv_sec;
  uint32_t tv_nsec;

  timespec32() = default;

  timespec32(const timespec &from)
      : tv_sec(from.tv_sec),
        tv_nsec(from.tv_nsec) {
  }
};

struct gce_audio_message {
//  static const size32_t kMaxAudioFrameLen = 65536;
  enum message_t {
    UNKNOWN = 0,
    DATA_SAMPLES = 1,
    OPEN_INPUT_STREAM = 2,
    OPEN_OUTPUT_STREAM = 3,
    CLOSE_INPUT_STREAM = 4,
    CLOSE_OUTPUT_STREAM = 5,
    CONTROL_PAUSE = 100
  };
  // Size of the header + data. Used to frame when we're on TCP.
  size32_t total_size;
  // Size of the audio header
  size32_t header_size;
  message_t message_type;
  // Identifier for the stream.
  uint32_t stream_number;
  // HAL assigned frame number, starts from 0.
  int64_t frame_num;
  // MONOTONIC_TIME when these frames were presented to the HAL.
  timespec32 time_presented;
  // Sample rate from the audio configuration.
  uint32_t frame_rate;
  // Channel mask from the audio configuration.
  audio_channel_mask_t channel_mask;
  // Format from the audio configuration.
  audio_format_t format;
  // Size of each frame in bytes.
  size32_t frame_size;
  // Number of frames that were presented to the HAL.
  size32_t num_frames_presented;
  // Number of frames that the HAL accepted.
  //   For blocking audio this will be the same as num_frames.
  //   For non-blocking audio this may be less.
  size32_t num_frames_accepted;
  // Count of the number of packets that were dropped because they would
  // have blocked the HAL or exceeded the maximum message size.
  size32_t num_packets_dropped;
  // Count of the number of packets that were shortened to fit within
  // kMaxAudioFrameLen.
  size32_t num_packets_shortened;
  // num_frames_presented (not num_frames_accepted) will follow here.

  gce_audio_message() :
      total_size(sizeof(gce_audio_message)),
      header_size(sizeof(gce_audio_message)),
      message_type(UNKNOWN),
      stream_number(0),
      frame_num(0),
      frame_rate(0),
      channel_mask(0),
      format(AUDIO_FORMAT_DEFAULT),
      frame_size(0),
      num_frames_presented(0),
      num_frames_accepted(0),
      num_packets_dropped(0),
      num_packets_shortened(0) {
    time_presented.tv_sec = 0;
    time_presented.tv_nsec = 0;
  }
};
  
}

struct AudioSource::Encoder {
    explicit Encoder();
    virtual ~Encoder() = default;

    virtual status_t initCheck() const = 0;
    virtual void encode(const void *data, size_t size) = 0;
    virtual void reset() = 0;

    void setFrameCallback(
            std::function<void(const sp<ABuffer> &)> onFrameFn);

protected:
    std::function<void(const sp<ABuffer> &)> mOnFrameFn;
};

AudioSource::Encoder::Encoder()
    : mOnFrameFn(nullptr) {
}

void AudioSource::Encoder::setFrameCallback(
        std::function<void(const sp<ABuffer> &)> onFrameFn) {
    mOnFrameFn = onFrameFn;
}

struct AudioSource::AACEncoder : public AudioSource::Encoder {
    explicit AACEncoder(bool useADTSFraming);
    ~AACEncoder() override;

    status_t initCheck() const override;

    AACEncoder(const AACEncoder &) = delete;
    AACEncoder &operator=(const AACEncoder &) = delete;

    void encode(const void *data, size_t size) override;
    void reset() override;

private:
    static constexpr unsigned kAACProfile = AOT_AAC_LC;

    status_t mInitCheck;

    bool mUseADTSFraming;

    gce_audio_message mPrevHeader;
    bool mPrevHeaderValid;

    HANDLE_AACENCODER mImpl;

    sp<ABuffer> mConfig;
    sp<ABuffer> mInputFrame;

    size_t mADTSSampleRateIndex;
    size_t mChannelCount;

    FILE *mLogFile;

    void fillADTSHeader(const sp<ABuffer> &outBuffer) const;
    static bool GetSampleRateIndex(int32_t sampleRate, size_t *tableIndex);
};

AudioSource::AACEncoder::AACEncoder(bool useADTSFraming)
    : mInitCheck(NO_INIT),
      mUseADTSFraming(useADTSFraming),
      mPrevHeaderValid(false),
      mImpl(nullptr),
      mADTSSampleRateIndex(0),
      mChannelCount(0),
      mLogFile(nullptr) {
    reset();

    if (mImpl != nullptr) {
        mInitCheck = OK;
    }
}

AudioSource::AACEncoder::~AACEncoder() {
    if (mLogFile != nullptr) {
        fclose(mLogFile);
        mLogFile = nullptr;
    }

    if (mImpl != nullptr) {
        aacEncClose(&mImpl);
        mImpl = nullptr;
    }
}

status_t AudioSource::AACEncoder::initCheck() const {
    return mInitCheck;
}

void AudioSource::AACEncoder::reset() {
    if (mLogFile != nullptr) {
        fclose(mLogFile);
        mLogFile = nullptr;
    }

#if LOG_AUDIO
    mLogFile = fopen("/tmp/log_remote.aac", "wb");
    CHECK(mLogFile != nullptr);
#endif

    if (mImpl != nullptr) {
        aacEncClose(&mImpl);
        mImpl = nullptr;
    }

    if (aacEncOpen(&mImpl, 0, 0) != AACENC_OK) {
        mImpl = nullptr;
        return;
    }

    mPrevHeaderValid = false;
}

void AudioSource::AACEncoder::encode(const void *_data, size_t size) {
    auto data = static_cast<const uint8_t *>(_data);

    CHECK_GE(size, sizeof(gce_audio_message));

    gce_audio_message hdr;
    std::memcpy(&hdr, data, sizeof(gce_audio_message));

    if (hdr.message_type != gce_audio_message::DATA_SAMPLES) {
        return;
    }

    int64_t timeUs =
        static_cast<int64_t>(hdr.time_presented.tv_sec) * 1000000ll
        + (hdr.time_presented.tv_nsec + 500) / 1000;

    if (!mPrevHeaderValid
            || mPrevHeader.frame_size != hdr.frame_size
            || mPrevHeader.frame_rate != hdr.frame_rate
            || mPrevHeader.stream_number != hdr.stream_number) {

        if (mPrevHeaderValid) {
            LOG(INFO) << "Found audio data in a different configuration than before!";

            // reset?
            return;
        }

        mPrevHeaderValid = true;
        mPrevHeader = hdr;

        CHECK_EQ(aacEncoder_SetParam(
                    mImpl, AACENC_AOT, kAACProfile), AACENC_OK);

        CHECK_EQ(aacEncoder_SetParam(
                    mImpl, AACENC_SAMPLERATE, hdr.frame_rate), AACENC_OK);

        CHECK_EQ(aacEncoder_SetParam(mImpl, AACENC_BITRATE, 128000), AACENC_OK);

        const size_t numChannels = hdr.frame_size / sizeof(int16_t);
        CHECK(numChannels == 1 || numChannels == 2);

        mChannelCount = numChannels;

        CHECK_EQ(aacEncoder_SetParam(
                    mImpl,
                    AACENC_CHANNELMODE,
                    (numChannels == 1) ? MODE_1 : MODE_2),
                AACENC_OK);

        CHECK_EQ(aacEncoder_SetParam(
                    mImpl, AACENC_TRANSMUX, TT_MP4_RAW), AACENC_OK);

        CHECK_EQ(aacEncEncode(
                    mImpl, nullptr, nullptr, nullptr, nullptr), AACENC_OK);

        AACENC_InfoStruct encInfo;
        CHECK_EQ(aacEncInfo(mImpl, &encInfo), AACENC_OK);

        mConfig = new ABuffer(encInfo.confSize);
        memcpy(mConfig->data(), encInfo.confBuf, encInfo.confSize);

        // hexdump(mConfig->data(), mConfig->size(), 0, nullptr);

        if (!mUseADTSFraming) {
            if (mOnFrameFn) {
                mOnFrameFn(mConfig);
            }
        } else {
            CHECK(GetSampleRateIndex(hdr.frame_rate, &mADTSSampleRateIndex));
        }

        const size_t numBytesPerInputFrame =
            numChannels * 1024 * sizeof(int16_t);

        mInputFrame = new ABuffer(numBytesPerInputFrame);
        mInputFrame->setRange(0, 0);
    }

    size_t offset = sizeof(gce_audio_message);
    while (offset < size) {
        if (mInputFrame->size() == 0) {
            mInputFrame->meta()->setInt64("timeUs", timeUs);
        }

        size_t copy = std::min(
                size - offset, mInputFrame->capacity() - mInputFrame->size());

        memcpy(mInputFrame->data() + mInputFrame->size(), &data[offset], copy);
        mInputFrame->setRange(0, mInputFrame->size() + copy);

        offset += copy;

        // "Time" on the input data has in effect advanced by the
        // number of audio frames we just advanced offset by.
        timeUs +=
            (copy * 1000000ll / hdr.frame_rate) / (mChannelCount * sizeof(int16_t));

        if (mInputFrame->size() == mInputFrame->capacity()) {
            void *inBuffers[] = { nullptr };
            INT inBufferIds[] = { IN_AUDIO_DATA };
            INT inBufferSizes[] = { 0 };
            INT inBufferElSizes[] = { sizeof(int16_t) };

            AACENC_BufDesc inBufDesc;
            inBufDesc.numBufs = sizeof(inBuffers) / sizeof(inBuffers[0]);
            inBufDesc.bufs = inBuffers;
            inBufDesc.bufferIdentifiers = inBufferIds;
            inBufDesc.bufSizes = inBufferSizes;
            inBufDesc.bufElSizes = inBufferElSizes;

            static constexpr size_t kMaxFrameSize = 8192;
            static constexpr size_t kADTSHeaderSize = 7;

            sp<ABuffer> outBuffer =
                new ABuffer(
                        mUseADTSFraming
                            ? kMaxFrameSize + kADTSHeaderSize : kMaxFrameSize);

            if (mUseADTSFraming) {
                outBuffer->setRange(0, kADTSHeaderSize);
            } else {
                outBuffer->setRange(0, 0);
            }

            void *outBuffers[] = { nullptr };
            INT outBufferIds[] = { OUT_BITSTREAM_DATA };
            INT outBufferSizes[] = { 0 };
            INT outBufferElSizes[] = { sizeof(UCHAR) };

            AACENC_BufDesc outBufDesc;
            outBufDesc.numBufs = sizeof(outBuffers) / sizeof(outBuffers[0]);
            outBufDesc.bufs = outBuffers;
            outBufDesc.bufferIdentifiers = outBufferIds;
            outBufDesc.bufSizes = outBufferSizes;
            outBufDesc.bufElSizes = outBufferElSizes;

            size_t inSampleOffset = 0;
            do {
                AACENC_InArgs inArgs;
                AACENC_OutArgs outArgs;
                memset(&inArgs, 0, sizeof(inArgs));
                memset(&outArgs, 0, sizeof(outArgs));

                inArgs.numInSamples =
                    mInputFrame->size() / sizeof(int16_t) - inSampleOffset;

                inBuffers[0] =
                    mInputFrame->data() + inSampleOffset * sizeof(int16_t);

                inBufferSizes[0] = inArgs.numInSamples * sizeof(int16_t);

                outBuffers[0] = outBuffer->data() + outBuffer->size();
                outBufferSizes[0] = outBuffer->capacity() - outBuffer->size();

                CHECK_EQ(aacEncEncode(
                            mImpl, &inBufDesc, &outBufDesc, &inArgs, &outArgs),
                         AACENC_OK);

                outBuffer->setRange(
                        0, outBuffer->size() + outArgs.numOutBytes);

                inSampleOffset += outArgs.numInSamples;
            } while (inSampleOffset < (mInputFrame->size() / sizeof(int16_t)));

            int64_t inputFrameTimeUs;
            CHECK(mInputFrame->meta()->findInt64("timeUs", &inputFrameTimeUs));
            outBuffer->meta()->setInt64("timeUs", inputFrameTimeUs);

            mInputFrame->setRange(0, 0);

            if (mUseADTSFraming) {
                fillADTSHeader(outBuffer);
            }

#if LOG_AUDIO
            fwrite(outBuffer->data(), 1, outBuffer->size(), mLogFile);
            fflush(mLogFile);
#endif

            if (mOnFrameFn) {
                mOnFrameFn(outBuffer);
            }
        }
    }
}

void AudioSource::AACEncoder::fillADTSHeader(const sp<ABuffer> &outBuffer) const {
    static constexpr unsigned kADTSId = 0;
    static constexpr unsigned kADTSLayer = 0;
    static constexpr unsigned kADTSProtectionAbsent = 1;

    unsigned frameLength = outBuffer->size();
    uint8_t *dst = outBuffer->data();

    dst[0] = 0xff;

    dst[1] =
        0xf0 | (kADTSId << 3) | (kADTSLayer << 1) | kADTSProtectionAbsent;

    dst[2] = ((kAACProfile - 1) << 6)
            | (mADTSSampleRateIndex << 2)
            | (mChannelCount >> 2);

    dst[3] = ((mChannelCount & 3) << 6) | (frameLength >> 11);

    dst[4] = (frameLength >> 3) & 0xff;
    dst[5] = (frameLength & 7) << 5;
    dst[6] = 0x00;
}

// static
bool AudioSource::AACEncoder::GetSampleRateIndex(
        int32_t sampleRate, size_t *tableIndex) {
    static constexpr int32_t kSampleRateTable[] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000
    };
    static constexpr size_t kNumSampleRates =
        sizeof(kSampleRateTable) / sizeof(kSampleRateTable[0]);

    *tableIndex = 0;
    for (size_t index = 0; index < kNumSampleRates; ++index) {
        if (sampleRate == kSampleRateTable[index]) {
            *tableIndex = index;
            return true;
        }
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////

struct Upsampler {
    explicit Upsampler(int32_t from = 44100, int32_t to = 48000)
        : mFrom(from),
          mTo(to),
          mCounter(0) {
    }

    void append(const int16_t *data, size_t numFrames) {
        for (size_t i = 0; i < numFrames; ++i) {
            int16_t l = *data++;
            int16_t r = *data++;

            mCounter += mTo;
            while (mCounter >= mFrom) {
                mCounter -= mFrom;

                mBuffer.push_back(l);
                mBuffer.push_back(r);
            }
        }
    }

    const int16_t *data() const { return mBuffer.data(); }

    size_t numFramesAvailable() const { return mBuffer.size() / 2; }

    void drain(size_t numFrames) {
        CHECK_LE(numFrames, numFramesAvailable());

        mBuffer.erase(mBuffer.begin(), mBuffer.begin() + numFrames * 2);
    }

private:
    int32_t mFrom;
    int32_t mTo;

    std::vector<int16_t> mBuffer;

    int32_t mCounter;
};

////////////////////////////////////////////////////////////////////////////////

struct AudioSource::OPUSEncoder : public AudioSource::Encoder {
    explicit OPUSEncoder();
    ~OPUSEncoder() override;

    status_t initCheck() const override;

    OPUSEncoder(const OPUSEncoder &) = delete;
    OPUSEncoder &operator=(const OPUSEncoder &) = delete;

    void encode(const void *data, size_t size) override;
    void reset() override;

private:
    status_t mInitCheck;

    gce_audio_message mPrevHeader;
    bool mPrevHeaderValid;

    size_t mChannelCount;

    OpusEncoder *mImpl;

    std::unique_ptr<Upsampler> mUpSampler;

    FILE *mLogFile;
};

AudioSource::OPUSEncoder::OPUSEncoder()
    : mInitCheck(NO_INIT),
      mImpl(nullptr),
      mLogFile(nullptr) {
    reset();
    mInitCheck = OK;
}

AudioSource::OPUSEncoder::~OPUSEncoder() {
    reset();
}

status_t AudioSource::OPUSEncoder::initCheck() const {
    return mInitCheck;
}

void AudioSource::OPUSEncoder::reset() {
    if (mLogFile != nullptr) {
        fclose(mLogFile);
        mLogFile = nullptr;
    }

    mUpSampler.reset();

    if (mImpl) {
        opus_encoder_destroy(mImpl);
        mImpl = nullptr;
    }

    mPrevHeaderValid = false;
    mChannelCount = 0;
}

void AudioSource::OPUSEncoder::encode(const void *_data, size_t size) {
    auto data = static_cast<const uint8_t *>(_data);

    CHECK_GE(size, sizeof(gce_audio_message));

    gce_audio_message hdr;
    std::memcpy(&hdr, data, sizeof(gce_audio_message));

    if (hdr.message_type != gce_audio_message::DATA_SAMPLES) {
        return;
    }

#if 0
    int64_t timeUs =
        static_cast<int64_t>(hdr.time_presented.tv_sec) * 1000000ll
        + (hdr.time_presented.tv_nsec + 500) / 1000;
#else
    static int64_t timeUs = 0;
#endif

    static int64_t prevTimeUs = 0;

    LOG(VERBOSE)
        << "encode received "
        << ((size - sizeof(gce_audio_message)) / (2 * sizeof(int16_t)))
        << " frames, "
        << " deltaTime = "
        << (((timeUs - prevTimeUs) * hdr.frame_rate) / 1000000ll)
        << " frames";

    prevTimeUs = timeUs;

    if (!mPrevHeaderValid
            || mPrevHeader.frame_size != hdr.frame_size
            || mPrevHeader.frame_rate != hdr.frame_rate
            || mPrevHeader.stream_number != hdr.stream_number) {

        if (mPrevHeaderValid) {
            LOG(INFO)
                << "Found audio data in a different configuration than before!"
                << " frame_size="
                << hdr.frame_size
                << " vs. "
                << mPrevHeader.frame_size
                << ", frame_rate="
                << hdr.frame_rate
                << " vs. "
                << mPrevHeader.frame_rate
                << ", stream_number="
                << hdr.stream_number
                << " vs. "
                << mPrevHeader.stream_number;

            // reset?
            return;
        }

        mPrevHeaderValid = true;
        mPrevHeader = hdr;

        const size_t numChannels = hdr.frame_size / sizeof(int16_t);

#if LOG_AUDIO
        mLogFile = fopen("/tmp/log_remote.opus", "wb");
        CHECK(mLogFile != nullptr);
#endif

        LOG(INFO)
            << "Calling opus_encoder_create w/ "
            << "hdr.frame_rate = "
            << hdr.frame_rate
            << ", numChannels = "
            << numChannels;

        int err;
        mImpl = opus_encoder_create(
                48000,
                numChannels,
                OPUS_APPLICATION_AUDIO,
                &err);

        CHECK_EQ(err, OPUS_OK);

        mChannelCount = numChannels;

        static_assert(sizeof(int16_t) == sizeof(opus_int16));

        err = opus_encoder_ctl(mImpl, OPUS_SET_INBAND_FEC(true));
        CHECK_EQ(err, OPUS_OK);

        err = opus_encoder_ctl(mImpl, OPUS_SET_PACKET_LOSS_PERC(10));
        CHECK_EQ(err, OPUS_OK);

        err = opus_encoder_ctl(
                mImpl, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));

        CHECK_EQ(err, OPUS_OK);

        CHECK_LE(hdr.frame_rate, 48000);
        mUpSampler = std::make_unique<Upsampler>(hdr.frame_rate, 48000);
    }

    // {2.5, 5, 10, 20, 40, 60, 80, 100, 120} ms
    static constexpr size_t kNumFramesPerOutputBuffer = 48 * 20;

    const size_t offset = sizeof(gce_audio_message);
    mUpSampler->append(
            reinterpret_cast<const int16_t *>(&data[offset]),
            (size - offset) / (mChannelCount * sizeof(int16_t)));

    while (mUpSampler->numFramesAvailable() >= kNumFramesPerOutputBuffer) {
        size_t copyFrames =
            std::min(mUpSampler->numFramesAvailable(),
                    kNumFramesPerOutputBuffer);

        static constexpr size_t kMaxPacketSize = 8192;

        sp<ABuffer> outBuffer = new ABuffer(kMaxPacketSize);

        auto outSize = opus_encode(
                mImpl,
                reinterpret_cast<const opus_int16 *>(mUpSampler->data()),
                copyFrames,
                outBuffer->data(),
                outBuffer->capacity());

        CHECK_GT(outSize, 0);

        outBuffer->setRange(0, outSize);

        outBuffer->meta()->setInt64("timeUs", timeUs);

        mUpSampler->drain(copyFrames);

        timeUs += (copyFrames * 1000ll) / 48;

#if LOG_AUDIO
        fwrite(outBuffer->data(), 1, outBuffer->size(), mLogFile);
        fflush(mLogFile);
#endif

        if (mOnFrameFn) {
            mOnFrameFn(outBuffer);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

struct Downsampler {
    explicit Downsampler(int32_t from = 44100, int32_t to = 8000)
        : mFrom(from),
          mTo(to),
          mCounter(0) {
    }

    void append(const int16_t *data, size_t numFrames) {
        for (size_t i = 0; i < numFrames; ++i) {
            int16_t l = *data++;
            int16_t r = *data++;

            mCounter += mTo;
            if (mCounter >= mFrom) {
                mCounter -= mFrom;

                auto mono = (l + r) / 2;
                mBuffer.push_back(mono);
            }
        }
    }

    const int16_t *data() const { return mBuffer.data(); }

    size_t numFramesAvailable() const { return mBuffer.size(); }

    void drain(size_t numFrames) {
        CHECK_LE(numFrames, numFramesAvailable());

        mBuffer.erase(mBuffer.begin(), mBuffer.begin() + numFrames);
    }

private:
    int32_t mFrom;
    int32_t mTo;

    std::vector<int16_t> mBuffer;

    int32_t mCounter;
};

struct AudioSource::G711Encoder : public AudioSource::Encoder {
    enum class Mode {
        ALAW,
        ULAW,
    };

    explicit G711Encoder(Mode mode);

    status_t initCheck() const override;

    G711Encoder(const G711Encoder &) = delete;
    G711Encoder &operator=(const G711Encoder &) = delete;

    void encode(const void *data, size_t size) override;
    void reset() override;

private:
    static constexpr size_t kNumFramesPerBuffer = 512;

    status_t mInitCheck;
    Mode mMode;

    gce_audio_message mPrevHeader;
    bool mPrevHeaderValid;

    size_t mChannelCount;

    sp<ABuffer> mOutputFrame;
    Downsampler mDownSampler;

    void doEncode(const int16_t *src, size_t numFrames);
};

AudioSource::G711Encoder::G711Encoder(Mode mode)
    : mInitCheck(NO_INIT),
      mMode(mode) {
    reset();
    mInitCheck = OK;
}

status_t AudioSource::G711Encoder::initCheck() const {
    return mInitCheck;
}

void AudioSource::G711Encoder::reset() {
    mPrevHeaderValid = false;
    mChannelCount = 0;
}

void AudioSource::G711Encoder::encode(const void *_data, size_t size) {
    auto data = static_cast<const uint8_t *>(_data);

    CHECK_GE(size, sizeof(gce_audio_message));

    gce_audio_message hdr;
    std::memcpy(&hdr, data, sizeof(gce_audio_message));

    if (hdr.message_type != gce_audio_message::DATA_SAMPLES) {
        return;
    }

#if 0
    int64_t timeUs =
        static_cast<int64_t>(hdr.time_presented.tv_sec) * 1000000ll
        + (hdr.time_presented.tv_nsec + 500) / 1000;
#else
    static int64_t timeUs = 0;
#endif

    static int64_t prevTimeUs = 0;

    LOG(VERBOSE)
        << "encode received "
        << ((size - sizeof(gce_audio_message)) / (2 * sizeof(int16_t)))
        << " frames, "
        << " deltaTime = "
        << ((timeUs - prevTimeUs) * 441) / 10000
        << " frames";

    prevTimeUs = timeUs;

    if (!mPrevHeaderValid
            || mPrevHeader.frame_size != hdr.frame_size
            || mPrevHeader.frame_rate != hdr.frame_rate
            || mPrevHeader.stream_number != hdr.stream_number) {

        if (mPrevHeaderValid) {
            LOG(INFO)
                << "Found audio data in a different configuration than before!"
                << " frame_size="
                << hdr.frame_size
                << " vs. "
                << mPrevHeader.frame_size
                << ", frame_rate="
                << hdr.frame_rate
                << " vs. "
                << mPrevHeader.frame_rate
                << ", stream_number="
                << hdr.stream_number
                << " vs. "
                << mPrevHeader.stream_number;

            // reset?
            return;
        }

        mPrevHeaderValid = true;
        mPrevHeader = hdr;

        mChannelCount = hdr.frame_size / sizeof(int16_t);

        // mono, 8-bit output samples.
        mOutputFrame = new ABuffer(kNumFramesPerBuffer);
    }

    const size_t offset = sizeof(gce_audio_message);
    mDownSampler.append(
            reinterpret_cast<const int16_t *>(&data[offset]),
            (size - offset) / (mChannelCount * sizeof(int16_t)));

    while (mDownSampler.numFramesAvailable() >= kNumFramesPerBuffer) {
        doEncode(mDownSampler.data(), kNumFramesPerBuffer);

        mOutputFrame->meta()->setInt64("timeUs", timeUs);

        mDownSampler.drain(kNumFramesPerBuffer);

        timeUs += (kNumFramesPerBuffer * 1000ll) / 8;

        if (mOnFrameFn) {
            mOnFrameFn(mOutputFrame);
        }
    }
}

static unsigned clz16(uint16_t x) {
    unsigned n = 0;
    if ((x & 0xff00) == 0) {
        n += 8;
        x <<= 8;
    }
    if ((x & 0xf000) == 0) {
        n += 4;
        x <<= 4;
    }

    static const unsigned kClzNibble[] = {
        4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0
    };

    return n + kClzNibble[n >> 12];
}

void AudioSource::G711Encoder::doEncode(const int16_t *src, size_t numFrames) {
    switch (mMode) {
        case Mode::ALAW:
        {
            uint8_t *dst = mOutputFrame->data();

            for (size_t i = numFrames; i--;) {
                uint16_t in = (*src++) >> 3;  // Convert from 16-bit to 13-bit.
                uint8_t inverseSign = 0x80;

                if (in & 0x8000) {
                    in = ~in;
                    inverseSign = 0x00;
                }

                auto numLeadingZeroes = clz16(in);
                auto suffixLength = 16 - numLeadingZeroes;

                static constexpr uint8_t kMask = 0x55;

                if (suffixLength <= 5) {
                    *dst++ = (((in >> 1) & 0x0f) | inverseSign) ^ kMask;
                } else {
                    auto shift = suffixLength - 5;
                    auto abcd = (in >> shift) & 0x0f;
                    *dst++ = (abcd | (shift << 4) | inverseSign) ^ kMask;
                }
            }
            break;
        }

        case Mode::ULAW:
        {
            uint8_t *dst = mOutputFrame->data();

            for (size_t i = numFrames; i--;) {
                uint16_t in = (*src++) >> 2;  // Convert from 16-bit to 14-bit.
                uint8_t inverseSign = 0x80;

                if (in & 0x8000) {
                    in = ~in;
                    inverseSign = 0x00;
                }

                in += 33;

                auto numLeadingZeroes = clz16(in);
                auto suffixLength = 16 - numLeadingZeroes;

                static constexpr uint8_t kMask = 0xff;

                if (suffixLength <= 6) {
                    *dst++ = (((in >> 1) & 0x0f) | inverseSign) ^ kMask;
                } else {
                    auto shift = suffixLength - 5;
                    auto abcd = (in >> shift) & 0x0f;
                    *dst++ = (abcd | ((shift - 1) << 4) | inverseSign) ^ kMask;
                }
            }
            break;
        }

        default:
            TRESPASS();
    }
}

////////////////////////////////////////////////////////////////////////////////

AudioSource::AudioSource(Format format, bool useADTSFraming)
    : mInitCheck(NO_INIT),
      mState(STOPPED)
#if SIMULATE_AUDIO
      ,mPhase(0)
#endif
{
    switch (format) {
        case Format::AAC:
        {
            mEncoder.reset(new AACEncoder(useADTSFraming));
            break;
        }

        case Format::OPUS:
        {
            CHECK(!useADTSFraming);
            mEncoder.reset(new OPUSEncoder);
            break;
        }

        case Format::G711_ALAW:
        case Format::G711_ULAW:
        {
            CHECK(!useADTSFraming);

            mEncoder.reset(
                    new G711Encoder(
                        (format == Format::G711_ALAW)
                            ? G711Encoder::Mode::ALAW
                            : G711Encoder::Mode::ULAW));
            break;
        }

        default:
            TRESPASS();
    }

    mEncoder->setFrameCallback([this](const sp<ABuffer> &accessUnit) {
        StreamingSource::onAccessUnit(accessUnit);
    });

    mInitCheck = OK;
}

AudioSource::~AudioSource() {
    stop();
}

status_t AudioSource::initCheck() const {
    return mInitCheck;
}

sp<AMessage> AudioSource::getFormat() const {
    return mFormat;
}

status_t AudioSource::start() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState != STOPPED) {
        return OK;
    }

    mEncoder->reset();

    mState = RUNNING;

#if SIMULATE_AUDIO
    mThread.reset(
            new std::thread([this]{
                auto startTime = std::chrono::steady_clock::now();

                std::vector<uint8_t> raw(
                        sizeof(gce_audio_message)
                            + kNumFramesPerBuffer * kNumChannels * sizeof(int16_t));

                gce_audio_message *buffer =
                        reinterpret_cast<gce_audio_message *>(raw.data());

                buffer->message_type = gce_audio_message::DATA_SAMPLES;
                buffer->frame_size = kNumChannels * sizeof(int16_t);
                buffer->frame_rate = kSampleRate;
                buffer->stream_number = 0;

                const double k = (double)kFrequency / kSampleRate * (2.0 * M_PI);

                while (mState != STOPPING) {
                    std::chrono::microseconds durationSinceStart(
                            (mPhase  * 1000000ll) / kSampleRate);

                    auto time = startTime + durationSinceStart;
                    auto now = std::chrono::steady_clock::now();
                    auto delayUs = std::chrono::duration_cast<
                            std::chrono::microseconds>(time - now).count();

                    if (delayUs > 0) {
                        usleep(delayUs);
                    }

                    auto usSinceStart =
                        std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - startTime).count();

                    buffer->time_presented.tv_sec = usSinceStart / 1000000ll;

                    buffer->time_presented.tv_nsec =
                        (usSinceStart % 1000000ll) * 1000;

                    int16_t *ptr =
                        reinterpret_cast<int16_t *>(
                                raw.data() + sizeof(gce_audio_message));

                    double x = mPhase * k;
                    for (size_t i = 0; i < kNumFramesPerBuffer; ++i) {
                        int16_t amplitude = (int16_t)(32767.0 * sin(x));

                        *ptr++ = amplitude;
                        if (kNumChannels == 2) {
                            *ptr++ = amplitude;
                        }

                        x += k;
                    }

                    mEncoder->encode(raw.data(), raw.size());

                    mPhase += kNumFramesPerBuffer;
                }
            }));
#else
/*
    if (mRegionView) {
        mThread.reset(
                new std::thread([this]{
                    while (mState != STOPPING) {
                        uint8_t buffer[4096];

                        struct timespec absTimeLimit;
                        vsoc::RegionView::GetFutureTime(
                                1000000000ll ns_from_now, &absTimeLimit);

                        intptr_t res = mRegionView->data()->audio_queue.Read(
                                mRegionView,
                                reinterpret_cast<char *>(buffer),
                                sizeof(buffer),
                                &absTimeLimit);

                        if (res < 0) {
                            if (res == -ETIMEDOUT) {
                                LOG(VERBOSE) << "AudioSource read timed out";
                            }
                            continue;
                        }

                        if (mState == RUNNING) {
                            mEncoder->encode(buffer, static_cast<size_t>(res));
                        }
                    }
            }));
    }
    */
#endif  // SIMULATE_AUDIO

    return OK;
}

status_t AudioSource::stop() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == STOPPED) {
        return OK;
    }

    mState = STOPPING;

    if (mThread) {
        mThread->join();
        mThread.reset();
    }

    mState = STOPPED;

    return OK;
}

status_t AudioSource::requestIDRFrame() {
    return OK;
}

void AudioSource::inject(const void *data, size_t size) {
    // Only used in the case of CrosVM operation.

    std::lock_guard<std::mutex> autoLock(mLock);
    if (mState != State::RUNNING) {
        return;
    }

    mEncoder->encode(static_cast<const uint8_t *>(data), size);
}

}  // namespace android

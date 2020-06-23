/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <source/AudioSource.h>

#include <libyuv/convert.h>

#include <system/audio.h>

#include "host/libs/config/cuttlefish_config.h"

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

    virtual int32_t initCheck() const = 0;
    virtual void encode(const void *data, size_t size) = 0;
    virtual void reset() = 0;

    void setFrameCallback(
            std::function<void(const std::shared_ptr<SBuffer> &)> onFrameFn);

protected:
    std::function<void(const std::shared_ptr<SBuffer> &)> mOnFrameFn;
};

AudioSource::Encoder::Encoder()
    : mOnFrameFn(nullptr) {
}

void AudioSource::Encoder::setFrameCallback(
        std::function<void(const std::shared_ptr<SBuffer> &)> onFrameFn) {
    mOnFrameFn = onFrameFn;
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

    int32_t initCheck() const override;

    OPUSEncoder(const OPUSEncoder &) = delete;
    OPUSEncoder &operator=(const OPUSEncoder &) = delete;

    void encode(const void *data, size_t size) override;
    void reset() override;

private:
    int32_t mInitCheck;

    gce_audio_message mPrevHeader;
    bool mPrevHeaderValid;

    size_t mChannelCount;

    OpusEncoder *mImpl;

    std::unique_ptr<Upsampler> mUpSampler;

    FILE *mLogFile;
};

AudioSource::OPUSEncoder::OPUSEncoder()
    : mInitCheck(-ENODEV),
      mImpl(nullptr),
      mLogFile(nullptr) {
    reset();
    mInitCheck = 0;
}

AudioSource::OPUSEncoder::~OPUSEncoder() {
    reset();
}

int32_t AudioSource::OPUSEncoder::initCheck() const {
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

    static int64_t timeUs = 0;

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

        std::shared_ptr<SBuffer> outBuffer(new SBuffer(kMaxPacketSize));

        auto outSize = opus_encode(
                mImpl,
                reinterpret_cast<const opus_int16 *>(mUpSampler->data()),
                copyFrames,
                outBuffer->data(),
                outBuffer->capacity());

        CHECK_GT(outSize, 0);

        outBuffer->resize(outSize);

        outBuffer->time_us(timeUs);

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

    int32_t initCheck() const override;

    G711Encoder(const G711Encoder &) = delete;
    G711Encoder &operator=(const G711Encoder &) = delete;

    void encode(const void *data, size_t size) override;
    void reset() override;

private:
    static constexpr size_t kNumFramesPerBuffer = 512;

    int32_t mInitCheck;
    Mode mMode;

    gce_audio_message mPrevHeader;
    bool mPrevHeaderValid;

    size_t mChannelCount;

    std::shared_ptr<SBuffer> mOutputFrame;
    Downsampler mDownSampler;

    void doEncode(const int16_t *src, size_t numFrames);
};

AudioSource::G711Encoder::G711Encoder(Mode mode)
    : mInitCheck(-ENODEV),
      mMode(mode) {
    reset();
    mInitCheck = 0;
}

int32_t AudioSource::G711Encoder::initCheck() const {
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

    static int64_t timeUs = 0;

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
        mOutputFrame.reset(new SBuffer(kNumFramesPerBuffer));
    }

    const size_t offset = sizeof(gce_audio_message);
    mDownSampler.append(
            reinterpret_cast<const int16_t *>(&data[offset]),
            (size - offset) / (mChannelCount * sizeof(int16_t)));

    while (mDownSampler.numFramesAvailable() >= kNumFramesPerBuffer) {
        doEncode(mDownSampler.data(), kNumFramesPerBuffer);

        mOutputFrame->time_us(timeUs);

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
            LOG(FATAL) << "Should not be here.";
    }
}

////////////////////////////////////////////////////////////////////////////////

AudioSource::AudioSource(Format format, bool useADTSFraming)
    : mInitCheck(-ENODEV),
      mState(STOPPED)
#if SIMULATE_AUDIO
      ,mPhase(0)
#endif
{
    switch (format) {
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
            LOG(FATAL) << "Should not be here.";
    }

    mEncoder->setFrameCallback([this](const std::shared_ptr<SBuffer> &accessUnit) {
        StreamingSource::onAccessUnit(accessUnit);
    });

    mInitCheck = 0;
}

AudioSource::~AudioSource() {
    stop();
}

int32_t AudioSource::initCheck() const {
    return mInitCheck;
}

int32_t AudioSource::start() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState != STOPPED) {
        return 0;
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
                        cuttlefish::RegionView::GetFutureTime(
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

    return 0;
}

int32_t AudioSource::stop() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == STOPPED) {
        return 0;
    }

    mState = STOPPING;

    if (mThread) {
        mThread->join();
        mThread.reset();
    }

    mState = STOPPED;

    return 0;
}

int32_t AudioSource::requestIDRFrame() {
    return 0;
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

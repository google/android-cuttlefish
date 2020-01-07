//#define LOG_NDEBUG 0
#define LOG_TAG "AACPlayer"
#include <utils/Log.h>

#include <source/AACPlayer.h>

#include <algorithm>

namespace android {

static void check(OSStatus err, const char *file, int line) {
    if (err == noErr) {
        return;
    }

    ALOGE("check FAILED w/ error %d at %s:%d", err, file, line);
    TRESPASS();
}

#define CHECK_OSSTATUS(err) do { check(err, __FILE__, __LINE__); } while (0)

static constexpr int32_t kSampleRate[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000
};

AACPlayer::AACPlayer()
    : mConverter(nullptr),
#if USE_AUDIO_UNIT
      mGraph(nullptr),
#else
      mQueue(nullptr),
#endif
      mSampleRateHz(-1),
      mNumFramesSubmitted(0) {
}

AACPlayer::~AACPlayer() {
#if USE_AUDIO_UNIT
    if (mGraph) {
        DisposeAUGraph(mGraph);
        mGraph = nullptr;
    }
#else
    if (mQueue) {
        AudioQueueStop(mQueue, true /* immediate */);
        AudioQueueDispose(mQueue, true /* immediate */);
        mQueue = nullptr;
    }
#endif

    if (mConverter) {
        AudioConverterDispose(mConverter);
        mConverter = nullptr;
    }
}

struct FeedCookie {
    const void *data;
    size_t size;
};

static OSStatus FeedInputData(
        AudioConverterRef /* converter */,
        UInt32 *numDataPackets,
        AudioBufferList *data,
        AudioStreamPacketDescription **dataPacketDescription,
        void *_cookie) {
    FeedCookie *cookie = static_cast<FeedCookie *>(_cookie);

    assert(*numDataPackets == 1);
    assert(data->mNumberBuffers == 1);

    assert(cookie->size > 0);
    data->mBuffers[0].mNumberChannels = 0;
    data->mBuffers[0].mDataByteSize = static_cast<UInt32>(cookie->size);
    data->mBuffers[0].mData = const_cast<void *>(cookie->data);

    if (dataPacketDescription) {
        static AudioStreamPacketDescription desc;
        desc.mDataByteSize = static_cast<UInt32>(cookie->size);
        desc.mStartOffset = 0;
        desc.mVariableFramesInPacket = 0;

        *dataPacketDescription = &desc;
    }

    cookie->size = 0;

    return noErr;
}

status_t AACPlayer::feedADTSFrame(const void *_frame, size_t size) {
    const uint8_t *frame = static_cast<const uint8_t *>(_frame);

    static constexpr size_t kADTSHeaderSize = 7;

    if (size < kADTSHeaderSize) {
        return -EINVAL;
    }

    if (frame[0] != 0xff || (frame[1] >> 4) != 0xf) {
        return -EINVAL;
    }

    const size_t frameSize =
        (static_cast<size_t>(frame[3]) & 0x3f) << 11
        | static_cast<size_t>(frame[4]) << 3
        | frame[5] >> 5;

    if (size != frameSize) {
        return -EINVAL;
    }

    if (mConverter == nullptr) {
        // size_t profile = (frame[2] >> 6) + 1;
        int32_t sampleRateIndex = (frame[2] >> 2) & 15;
        int32_t channelCount = ((frame[2] & 3) << 2) | (frame[3] >> 6);

        int32_t sampleRate = kSampleRate[sampleRateIndex];

        status_t err = init(sampleRateIndex, channelCount);
        if (err != OK) {
            return err;
        }

        mSampleRateHz = sampleRate;
    }

#if USE_AUDIO_UNIT
    struct OutputBuffer {
        void *mAudioData;
        UInt32 mAudioDataByteSize;
    };

    const size_t outBufferSize = mBufferQueue->bufferSize();
    std::unique_ptr<OutputBuffer> outBuffer(new OutputBuffer);
    outBuffer->mAudioData = mBufferQueue->acquire();
    outBuffer->mAudioDataByteSize = 0;
#else
    const size_t outBufferSize = mBufferManager->bufferSize();
    AudioQueueBufferRef outBuffer = mBufferManager->acquire();
#endif

    UInt32 outputDataPacketSize = mInFormat.mFramesPerPacket;
    AudioBufferList outputData;
    outputData.mNumberBuffers = 1;
    outputData.mBuffers[0].mData = outBuffer->mAudioData;
    outputData.mBuffers[0].mDataByteSize = static_cast<UInt32>(outBufferSize);
    outputData.mBuffers[0].mNumberChannels = mInFormat.mChannelsPerFrame;

    FeedCookie cookie;
    cookie.data = &frame[kADTSHeaderSize];
    cookie.size = frameSize - kADTSHeaderSize;

    OSStatus err = AudioConverterFillComplexBuffer(
            mConverter,
            FeedInputData,
            &cookie,
            &outputDataPacketSize,
            &outputData,
            nullptr);

    CHECK_OSSTATUS(err);

    assert(outputDataPacketSize == mInFormat.mFramesPerPacket);
    assert(outputData.mNumberBuffers == 1);

    outBuffer->mAudioDataByteSize = outputData.mBuffers[0].mDataByteSize;

#if USE_AUDIO_UNIT
    mBufferQueue->queue(outBuffer->mAudioData);
#else
    err = AudioQueueEnqueueBuffer(
            mQueue,
            outBuffer,
            0 /* numPacketDescs */,
            nullptr /* packetDescs */);

    CHECK_OSSTATUS(err);
#endif

    mNumFramesSubmitted += 1024;

    return OK;
}

int32_t AACPlayer::sampleRateHz() const {
    return mSampleRateHz;
}

static void writeInt16(uint8_t *&ptr, uint16_t x) {
    *ptr++ = x >> 8;
    *ptr++ = x & 0xff;
}

static void writeInt32(uint8_t *&ptr, uint32_t x) {
    writeInt16(ptr, x >> 16);
    writeInt16(ptr, x & 0xffff);
}

static void writeInt24(uint8_t *&ptr, uint32_t x) {
    *ptr++ = (x >> 16) & 0xff;
    writeInt16(ptr, x & 0xffff);
}

static void writeDescriptor(uint8_t *&ptr, uint8_t tag, size_t size) {
    *ptr++ = tag;
    for (size_t i = 3; i > 0; --i) {
        *ptr++ = (size >> (7 * i)) | 0x80;
    }
    *ptr++ = size & 0x7f;
}

#if !USE_AUDIO_UNIT
static void PropertyListenerCallback(
        void * /* cookie */,
        AudioQueueRef queue,
        AudioQueuePropertyID /* propertyID */) {
    UInt32 isRunning;
    UInt32 size = sizeof(isRunning);

    OSStatus err = AudioQueueGetProperty(
            queue, kAudioQueueProperty_IsRunning, &isRunning, &size);

    CHECK_OSSTATUS(err);
    CHECK_EQ(size, sizeof(isRunning));

    ALOGI("AudioQueue is now %s", isRunning ? "running" : "stopped");
}
#else
// static
OSStatus AACPlayer::FeedInput(
        void *cookie,
        AudioUnitRenderActionFlags * /* flags */,
        const AudioTimeStamp * /* timeStamp */,
        UInt32 /* bus */,
        UInt32 numFrames,
        AudioBufferList *data) {
    AACPlayer *me = static_cast<AACPlayer *>(cookie);

    UInt32 curFrame = 0;
    void *outPtr = data->mBuffers[0].mData;

    const size_t bytesPerFrame = me->mOutFormat.mBytesPerFrame;

    while (curFrame < numFrames) {
        size_t inSize;
        const void *inData = me->mBufferQueue->dequeueBegin(&inSize);

        if (inData == nullptr) {
            // underrun
            memset(outPtr, 0, (numFrames - curFrame) * bytesPerFrame);
            break;
        }

        const size_t inSizeFrames = inSize / bytesPerFrame;

        const size_t copyFrames =
            std::min(inSizeFrames, static_cast<size_t>(numFrames - curFrame));

        const size_t copyBytes = copyFrames * bytesPerFrame;

        memcpy(outPtr, inData, copyBytes);
        outPtr = (uint8_t *)outPtr + copyBytes;

        me->mBufferQueue->dequeueEnd(inSize - copyBytes);

        curFrame += copyFrames;
    }

    data->mBuffers[0].mDataByteSize = numFrames * static_cast<UInt32>(bytesPerFrame);

    return noErr;
}
#endif

status_t AACPlayer::init(int32_t sampleRateIndex, size_t channelCount) {
    const int32_t sampleRate = kSampleRate[sampleRateIndex];

    memset(&mOutFormat, 0, sizeof(mOutFormat));

    mOutFormat.mSampleRate = static_cast<double>(sampleRate);
    mOutFormat.mBitsPerChannel = 8 * sizeof(float);
    mOutFormat.mChannelsPerFrame = static_cast<UInt32>(channelCount);

    mOutFormat.mFormatID = kAudioFormatLinearPCM;
    mOutFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;

    mOutFormat.mFramesPerPacket = 1;

    mOutFormat.mBytesPerFrame =
        (mOutFormat.mBitsPerChannel / 8) * mOutFormat.mChannelsPerFrame;

    mOutFormat.mBytesPerPacket =
        mOutFormat.mBytesPerFrame * mOutFormat.mFramesPerPacket;

    memset(&mInFormat, 0, sizeof(mInFormat));

    mInFormat.mSampleRate = static_cast<double>(sampleRate);
    mInFormat.mBitsPerChannel = 0;  // compressed
    mInFormat.mChannelsPerFrame = static_cast<UInt32>(channelCount);

    mInFormat.mFormatID = kAudioFormatMPEG4AAC;
    mInFormat.mFormatFlags = kMPEG4Object_AAC_LC;

    mInFormat.mFramesPerPacket = 1024;

    mInFormat.mBytesPerFrame = 0;  // variable
    mInFormat.mBytesPerPacket = 0;  // variable

    OSStatus err = AudioConverterNew(&mInFormat, &mOutFormat, &mConverter);
    CHECK_OSSTATUS(err);
    assert(mConverter != nullptr);

    // static constexpr uint8_t kAACCodecSpecificData[] = {0x11,0x90};
    // static constexpr uint8_t kAACCodecSpecificData[] = {0x12,0x10};

    // 5 bits: object type
    // 4 bits: frequency index
    // if (frequency index == 15) { 24 bits frequency }
    // 4 bits: channel config
    // 1 bit: frame length flag
    // 1 bit: dependsOnCoreCoder
    // 1 bit: extensionFlag

    // 0x11 0x90 => 00010 0011 0010 0 0 0 (AAC LC, 48kHz, 2 channels)
    // 0x12 0x10 => 00010 0100 0010 0 0 0 (AAC LC, 44.1kHz, 2 channels)

    uint8_t kAACCodecSpecificData[2];

    kAACCodecSpecificData[0] =
        (2 << 3) /* AAC LC */ | (sampleRateIndex >> 1);

    kAACCodecSpecificData[1] =
        ((sampleRateIndex & 1) << 7) | (channelCount << 3);

    static constexpr size_t kAACCodecSpecificDataSize =
        sizeof(kAACCodecSpecificData);

    uint8_t magic[128];
    uint8_t *ptr = magic;
    writeDescriptor(ptr, 0x03, 3 + 5 + 13 + 5 + kAACCodecSpecificDataSize);
    writeInt16(ptr, 0x00);
    *ptr++ = 0x00;

    // DecoderConfig descriptor
    writeDescriptor(ptr, 0x04, 13 + 5 + kAACCodecSpecificDataSize);

    // Object type indication
    *ptr++ = 0x40;

    // Flags (= Audiostream)
    *ptr++ = 0x15;

    writeInt24(ptr, 0);  // BufferSize DB
    writeInt32(ptr, 0);  // max bitrate
    writeInt32(ptr, 0);  // avg bitrate

    writeDescriptor(ptr, 0x05, kAACCodecSpecificDataSize);
    memcpy(ptr, kAACCodecSpecificData, kAACCodecSpecificDataSize);
    ptr += kAACCodecSpecificDataSize;

    size_t magicSize = ptr - magic;

    err = AudioConverterSetProperty(
            mConverter,
            kAudioConverterDecompressionMagicCookie,
            static_cast<UInt32>(magicSize),
            magic);

    CHECK_OSSTATUS(err);

#if USE_AUDIO_UNIT
    err = NewAUGraph(&mGraph);
    CHECK_OSSTATUS(err);

    AudioComponentDescription desc;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentType = kAudioUnitType_Output;

#ifdef TARGET_IOS
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif

    err = AUGraphAddNode(mGraph, &desc, &mOutputNode);
    CHECK_OSSTATUS(err);

    struct AURenderCallbackStruct cb;
    cb.inputProc = FeedInput;
    cb.inputProcRefCon = this;

    err = AUGraphSetNodeInputCallback(
            mGraph, mOutputNode, 0 /* inputNumber */, &cb);

    CHECK_OSSTATUS(err);

    err = AUGraphOpen(mGraph);
    CHECK_OSSTATUS(err);

    AudioUnit outputUnit;
    err = AUGraphNodeInfo(mGraph, mOutputNode, &desc, &outputUnit);
    CHECK_OSSTATUS(err);

    err = AudioUnitSetProperty(
            outputUnit,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            0 /* busNumber */,
            &mOutFormat,
            sizeof(mOutFormat));

    CHECK_OSSTATUS(err);

    err = AUGraphInitialize(mGraph);
    CHECK_OSSTATUS(err);

    mBufferQueue.reset(
            new BufferQueue(
                8 /* count */,
                mInFormat.mFramesPerPacket
                    * mInFormat.mChannelsPerFrame * sizeof(float)));

    err = AUGraphStart(mGraph);
    CHECK_OSSTATUS(err);
#else
    err = AudioQueueNewOutput(
            &mOutFormat,
            PlayCallback,
            this,
            nullptr /* callbackRunLoop */,
            kCFRunLoopCommonModes,
            0 /* flags */,
            &mQueue);

    CHECK_OSSTATUS(err);

    UInt32 enablePitch = 1;
    err = AudioQueueSetProperty(
            mQueue,
            kAudioQueueProperty_EnableTimePitch,
            &enablePitch,
            sizeof(enablePitch));

    CHECK_OSSTATUS(err);

#if 0
    UInt32 pitchAlgorithm = kAudioQueueTimePitchAlgorithm_Spectral;
    err = AudioQueueSetProperty(
            mQueue,
            kAudioQueueProperty_TimePitchAlgorithm,
            &pitchAlgorithm,
            sizeof(pitchAlgorithm));

    CHECK_OSSTATUS(err);
#endif

    err = AudioQueueSetParameter(mQueue, kAudioQueueParam_PlayRate, 1.0 /* 0.99 */);
    CHECK_OSSTATUS(err);

    err = AudioQueueAddPropertyListener(
            mQueue,
            kAudioQueueProperty_IsRunning,
            PropertyListenerCallback,
            this);

    CHECK_OSSTATUS(err);

    mBufferManager.reset(
            new android::AudioQueueBufferManager(
                mQueue,
                32 /* count */,
                mInFormat.mFramesPerPacket * channelCount * sizeof(float)));

    err = AudioQueueStart(mQueue, nullptr /* startTime */);
    CHECK_OSSTATUS(err);
#endif

    return OK;
}

#if !USE_AUDIO_UNIT
// static
void AACPlayer::PlayCallback(
        void *cookie, AudioQueueRef /* queue */, AudioQueueBufferRef buffer) {
    AACPlayer *me = static_cast<AACPlayer *>(cookie);
    me->mBufferManager->release(buffer);
}
#endif

}  // namespace android


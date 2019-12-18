#include <source/DirectRenderer_iOS.h>

#include <media/stagefright/avc_utils.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

DirectRenderer_iOS::DirectRenderer_iOS()
    : mVideoFormatDescription(nullptr),
      mSession(nullptr) {
}

DirectRenderer_iOS::~DirectRenderer_iOS() {
    if (mVideoFormatDescription) {
        CFRelease(mVideoFormatDescription);
        mVideoFormatDescription = NULL;
    }

    if (mSession) {
        VTDecompressionSessionInvalidate(mSession);
        CFRelease(mSession);
        mSession = NULL;
    }
}

static void OnFrameReady(
        void *decompressionOutputRefCon,
        void *sourceFrameRefCon,
        OSStatus status,
        VTDecodeInfoFlags infoFlags,
        CVImageBufferRef imageBuffer,
        CMTime presentationTimeStamp,
        CMTime presentationDuration) {
    static_cast<DirectRenderer_iOS *>(
            decompressionOutputRefCon)->render(imageBuffer);
}

void DirectRenderer_iOS::setFormat(size_t index, const sp<AMessage> &format) {
    ALOGI("DirectRenderer_iOS::setFormat(%zu) => %s",
          index,
          format->debugString().c_str());

    sp<ABuffer> csd0;
    CHECK(format->findBuffer("csd-0", &csd0));
    CHECK(csd0->size() >= 4 && !memcmp(csd0->data(), "\x00\x00\x00\x01", 4));

    sp<ABuffer> csd1;
    CHECK(format->findBuffer("csd-1", &csd1));
    CHECK(csd1->size() >= 4 && !memcmp(csd1->data(), "\x00\x00\x00\x01", 4));

    int32_t width, height;
    CHECK(format->findInt32("width", &width));
    CHECK(format->findInt32("height", &height));

    const uint8_t *parameterSets[2] = {
        csd0->data() + 4,
        csd1->data() + 4,
    };

    const size_t parameterSetSizes[2] = {
        csd0->size() - 4,
        csd1->size() - 4,
    };

    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
            kCFAllocatorDefault,
            sizeof(parameterSets) / sizeof(parameterSets[0]),
            parameterSets,
            parameterSetSizes,
            4 /* NALUnitHeaderLength */,
            &mVideoFormatDescription);

    CHECK_EQ(status, noErr);

    CFDictionaryRef videoDecoderSpecification = NULL;

    CFMutableDictionaryRef destinationImageBufferAttrs =
        CFDictionaryCreateMutable(
                kCFAllocatorDefault,
                0 /* capacity */,
                &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(
            destinationImageBufferAttrs,
            kCVPixelBufferOpenGLESCompatibilityKey,
            kCFBooleanTrue);

    SInt32 pixelType = kCVPixelFormatType_32BGRA;

    CFNumberRef value =
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixelType);

    CFDictionarySetValue(
            destinationImageBufferAttrs,
            kCVPixelBufferPixelFormatTypeKey,
            value);

    CFRelease(value);

    CFDictionaryRef surfaceProps =
        CFDictionaryCreate(
                kCFAllocatorDefault,
                NULL /* keys */,
                NULL /* values */,
                0 /* numValues */,
                &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(
            destinationImageBufferAttrs,
            kCVPixelBufferIOSurfacePropertiesKey,
            surfaceProps);

    CFRelease(surfaceProps);
    surfaceProps = NULL;

    VTDecompressionOutputCallbackRecord outputCallback = {
        .decompressionOutputCallback = OnFrameReady,
        .decompressionOutputRefCon = this,
    };

    status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        mVideoFormatDescription,
        videoDecoderSpecification,
        destinationImageBufferAttrs,
        &outputCallback,
        &mSession);

    CHECK_EQ(status, noErr);
}

static sp<ABuffer> ReplaceStartCodesWithLength(const sp<ABuffer> &buffer) {
    sp<ABuffer> outBuf = new ABuffer(buffer->size() + 128);  // Replacing 2 byte length with 4 byte startcodes takes its toll...
    uint8_t *outData = outBuf->data();
    size_t outOffset = 0;

    const uint8_t *data = buffer->data();
    size_t size = buffer->size();

    const uint8_t *nalStart;
    size_t nalSize;
    while (getNextNALUnit(&data, &size, &nalStart, &nalSize, true) == OK) {
        outData[outOffset++] = (nalSize >> 24) & 0xff;
        outData[outOffset++] = (nalSize >> 16) & 0xff;
        outData[outOffset++] = (nalSize >> 8) & 0xff;
        outData[outOffset++] = nalSize & 0xff;
        memcpy(&outData[outOffset], nalStart, nalSize);
        outOffset += nalSize;
    }

    outBuf->setRange(0, outOffset);

    return outBuf;
}

void DirectRenderer_iOS::queueAccessUnit(
        size_t index, const sp<ABuffer> &accessUnit) {
    sp<ABuffer> sampleBuf = ReplaceStartCodesWithLength(accessUnit);

    CMBlockBufferRef blockBuffer;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            sampleBuf->data(),
            sampleBuf->size(),
            kCFAllocatorNull,
            NULL /* customBlockSource */,
            0 /* offsetToData */,
            sampleBuf->size(),
            0 /* flags */,
            &blockBuffer);

    int64_t timeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

    const CMSampleTimingInfo timing = {
        .duration = 0,
        .presentationTimeStamp = CMTimeMake((timeUs * 9ll) / 100ll, 90000),
        .decodeTimeStamp = kCMTimeInvalid,
    };

    const size_t size = sampleBuf->size();

    CMSampleBufferRef sampleBuffer;
    status = CMSampleBufferCreate(
            kCFAllocatorDefault,
            blockBuffer,
            true /* dataReady */,
            NULL /* makeDataReadyCallback */,
            0 /* makeDataReadyRefCon */,
            mVideoFormatDescription,
            1 /* numSamples */,
            1 /* numSampleTimingEntries */,
            &timing /* sampleTimingArray */,
            1 /* numSampleSizeEntries */,
            &size /* sampleSizeArray */,
            &sampleBuffer);

    CFRelease(blockBuffer);
    blockBuffer = NULL;

    CHECK_EQ(status, noErr);

    // NSLog(@"got a buffer of size %zu\n", sampleBuf->size());

    VTDecodeInfoFlags infoFlags;
    status = VTDecompressionSessionDecodeFrame(
            mSession,
            sampleBuffer,
            kVTDecodeFrame_EnableAsynchronousDecompression
                | kVTDecodeFrame_EnableTemporalProcessing,
            0 /* sourceFrameRefCon */,
            &infoFlags);

    CFRelease(sampleBuffer);
    sampleBuffer = NULL;
}

}  // namespace android

#include <source/FrameBufferSource.h>

#include <algorithm>

#include <media/stagefright/Utils.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/TSPacketizer.h>
#include <libyuv/convert.h>

#include "host/libs/config/cuttlefish_config.h"

#include "vpx/vpx_encoder.h"
#include "vpx/vpx_codec.h"
#include "vpx/vp8cx.h"

#if ENABLE_H264
#include "x264/x264.h"
#endif

#include <gflags/gflags.h>

#define ENABLE_LOGGING          0

namespace android {

struct FrameBufferSource::Encoder {
    Encoder() = default;
    virtual ~Encoder() = default;

    virtual void forceIDRFrame() = 0;
    virtual bool isForcingIDRFrame() const = 0;
    virtual sp<ABuffer> encode(const void *frame, int64_t timeUs) = 0;

    virtual sp<AMessage> getFormat() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

#if ENABLE_H264

struct FrameBufferSource::H264Encoder : public FrameBufferSource::Encoder {
    H264Encoder(int width, int height, int rateHz);
    ~H264Encoder() override;

    void forceIDRFrame() override;
    bool isForcingIDRFrame() const override;
    sp<ABuffer> encode(const void *frame, int64_t timeUs) override;

    sp<AMessage> getFormat() const override;

private:
    sp<AMessage> mFormat;

    x264_param_t mParams;
    x264_picture_t mPicIn, mPicOut;
    x264_t *mImpl;

    int mWidth, mHeight;
    int32_t mNumFrames;

    int mSizeY, mSizeUV;

    std::atomic<bool> mForceIDRFrame;

    void *mI420Data;

#if ENABLE_LOGGING
    sp<TSPacketizer> mPacketizer;
    FILE *mFile;
#endif
};

static sp<ABuffer> copy(const void *data, size_t size) {
    sp<ABuffer> buffer = new ABuffer(size);
    memcpy(buffer->data(), data, size);
    return buffer;
}

FrameBufferSource::H264Encoder::H264Encoder(int width, int height, int rateHz)
    : mImpl(nullptr),
      mWidth(width),
      mHeight(height),
      mNumFrames(0),
      mForceIDRFrame(false),
      mI420Data(nullptr)
#if ENABLE_LOGGING
      ,mFile(fopen("/tmp/log.ts", "wb"))
#endif
{
    CHECK((width & 1) == 0);
    CHECK((height & 1) == 0);
    mSizeY = width * height;
    mSizeUV = (width / 2) * (height / 2);
    size_t totalSize = mSizeY + 2 * mSizeUV;
    mI420Data = malloc(totalSize);

    x264_param_default_preset(&mParams, "ultrafast", "zerolatency");

    mParams.i_threads = 4;
    mParams.i_width = width;
    mParams.i_height = height;
    mParams.i_fps_num = rateHz;
    mParams.i_fps_den = 1;
    mParams.i_bitdepth = 8;
    mParams.b_vfr_input = 0;
    mParams.b_repeat_headers = 1;
    mParams.b_annexb = 1;

    int csp = X264_CSP_I420;

    mParams.i_csp = csp;

    x264_param_apply_fastfirstpass(&mParams);
    x264_param_apply_profile(&mParams, "main");

    x264_picture_alloc(
            &mPicOut, csp, mParams.i_width, mParams.i_height);

    x264_picture_init(&mPicIn);
    mPicIn.img.i_csp = csp;

    mPicIn.img.i_stride[0] = width;
    mPicIn.img.i_stride[1] = width / 2;
    mPicIn.img.i_stride[2] = width / 2;
    mPicIn.img.i_plane = 3;

    mImpl = x264_encoder_open(&mParams);
    CHECK(mImpl);

    x264_nal_t *headers;
    int numNalUnits;
    /* int size = */x264_encoder_headers(mImpl, &headers, &numNalUnits);

    mFormat = new AMessage;
    mFormat->setString("mime", MEDIA_MIMETYPE_VIDEO_AVC);
    mFormat->setInt32("width", width);
    mFormat->setInt32("height", height);

    for (int i = 0; i < numNalUnits; ++i) {
        sp<ABuffer> csd = copy(headers[i].p_payload, headers[i].i_payload);
        mFormat->setBuffer(StringPrintf("csd-%d", i).c_str(), csd);
    }

    LOG(INFO) << "Format is " << mFormat->debugString().c_str();

#if ENABLE_LOGGING
    mPacketizer = new TSPacketizer;

    ssize_t videoTrackIndex = mPacketizer->addTrack(mFormat);
    LOG(INFO) << "Created video track index " << videoTrackIndex;
#endif
}

FrameBufferSource::H264Encoder::~H264Encoder() {
    // x264_picture_clean(&mPicOut);

    x264_encoder_close(mImpl);
    mImpl = nullptr;

    free(mI420Data);

#if ENABLE_LOGGING
    if (mFile) {
        fclose(mFile);
        mFile = nullptr;
    }
#endif
}

void FrameBufferSource::H264Encoder::forceIDRFrame() {
    mForceIDRFrame = true;
}

bool FrameBufferSource::H264Encoder::isForcingIDRFrame() const {
    return mForceIDRFrame;
}

sp<AMessage> FrameBufferSource::H264Encoder::getFormat() const {
    return mFormat;
}

sp<ABuffer> FrameBufferSource::H264Encoder::encode(
        const void *frame, int64_t timeUs) {
    if (frame) {
        // If we don't get a new frame, we'll just repeat the previously
        // YUV-converted frame again.

        mPicIn.img.plane[0] = (uint8_t *)mI420Data;
        mPicIn.img.plane[1] = (uint8_t *)mI420Data + mSizeY;
        mPicIn.img.plane[2] = (uint8_t *)mI420Data + mSizeY + mSizeUV;

        libyuv::ABGRToI420(
                static_cast<const uint8_t *>(frame),
                mWidth * 4,
                mPicIn.img.plane[0],
                mWidth,
                mPicIn.img.plane[1],
                mWidth / 2,
                mPicIn.img.plane[2],
                mWidth / 2,
                mWidth,
                mHeight);
    }

    if (mForceIDRFrame.exchange(false)) {
        mPicIn.i_type = X264_TYPE_IDR;
    } else {
        mPicIn.i_type = X264_TYPE_AUTO;
    }

    x264_nal_t *nals;
    int numNalUnits;

    int size = x264_encoder_encode(
            mImpl, &nals, &numNalUnits, &mPicIn, &mPicOut);

    // LOG(INFO) << "encoded frame of size " << size;

    sp<ABuffer> accessUnit = copy(nals[0].p_payload, size);
    accessUnit->meta()->setInt64("timeUs", timeUs);

#if ENABLE_LOGGING
    sp<ABuffer> packets;
    uint32_t flags = 0;

    if (mNumFrames == 0) {
        flags |= TSPacketizer::EMIT_PAT_AND_PMT;
    }

    if ((mNumFrames % 10) == 0) {
        flags |= TSPacketizer::EMIT_PCR;
    }

    status_t err = mPacketizer->packetize(
            0 /* trackIndex */,
            accessUnit,
            &packets,
            flags);

    CHECK(err == OK);

    if (mFile) {
        fwrite(packets->data(), 1, packets->size(), mFile);
        fflush(mFile);
    }
#endif

    ++mNumFrames;

    return accessUnit;
}

#endif  // ENABLE_H264

////////////////////////////////////////////////////////////////////////////////

struct FrameBufferSource::VPXEncoder : public FrameBufferSource::Encoder {
    VPXEncoder(int width, int height, int rateHz);
    ~VPXEncoder() override;

    void forceIDRFrame() override;
    bool isForcingIDRFrame() const override;

    sp<ABuffer> encode(const void *frame, int64_t timeUs) override;

    sp<AMessage> getFormat() const override;

private:
    int mWidth, mHeight, mRefreshRateHz;

    int mSizeY, mSizeUV;
    void *mI420Data;

    vpx_codec_iface_t *mCodecInterface;
    std::unique_ptr<vpx_codec_enc_cfg_t> mCodecConfiguration;

    using contextFreeFunc = std::function<vpx_codec_err_t(vpx_codec_ctx_t *)>;
    std::unique_ptr<vpx_codec_ctx_t, contextFreeFunc> mCodecContext;

    std::atomic<bool> mForceIDRFrame;
    bool mFirstFrame;
    int64_t mLastTimeUs;

    sp<AMessage> mFormat;
};

static int GetCPUCoreCount() {
    int cpuCoreCount;

#if defined(_SC_NPROCESSORS_ONLN)
    cpuCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
#else
    // _SC_NPROC_ONLN must be defined...
    cpuCoreCount = sysconf(_SC_NPROC_ONLN);
#endif

    CHECK_GE(cpuCoreCount, 1);
    return cpuCoreCount;
}

FrameBufferSource::VPXEncoder::VPXEncoder(int width, int height, int rateHz)
    : mWidth(width),
      mHeight(height),
      mRefreshRateHz(rateHz),
      mCodecContext(nullptr, vpx_codec_destroy),
      mForceIDRFrame(false),
      mFirstFrame(true),
      mLastTimeUs(0) {

    CHECK((width & 1) == 0);
    CHECK((height & 1) == 0);
    mSizeY = width * height;
    mSizeUV = (width / 2) * (height / 2);
    size_t totalSize = mSizeY + 2 * mSizeUV;
    mI420Data = malloc(totalSize);

    mCodecInterface = vpx_codec_vp8_cx();
    mCodecConfiguration = std::make_unique<vpx_codec_enc_cfg_t>();

    auto res = vpx_codec_enc_config_default(
            mCodecInterface, mCodecConfiguration.get(), 0 /* usage */);

    mCodecConfiguration->g_w = width;
    mCodecConfiguration->g_h = height;
    mCodecConfiguration->g_threads = std::min(GetCPUCoreCount(), 64);
    mCodecConfiguration->g_error_resilient = false;
    mCodecConfiguration->g_timebase.num = 1;
    mCodecConfiguration->g_timebase.den = 1000000;
    mCodecConfiguration->rc_target_bitrate = 2500;  // This appears to match x264
    mCodecConfiguration->rc_end_usage = VPX_VBR;
    mCodecConfiguration->rc_dropframe_thresh = 0;
    mCodecConfiguration->g_lag_in_frames = 0;

    mCodecConfiguration->g_profile = 0;

    CHECK_EQ(res, VPX_CODEC_OK);

    mCodecContext.reset(new vpx_codec_ctx_t);

    res = vpx_codec_enc_init(
            mCodecContext.get(),
            mCodecInterface,
            mCodecConfiguration.get(),
            0 /* flags */);

    CHECK_EQ(res, VPX_CODEC_OK);

    res = vpx_codec_control(mCodecContext.get(), VP8E_SET_TOKEN_PARTITIONS, 0);
    CHECK_EQ(res, VPX_CODEC_OK);

    mFormat = new AMessage;
    mFormat->setString("mime", MEDIA_MIMETYPE_VIDEO_VP8);
    mFormat->setInt32("width", width);
    mFormat->setInt32("height", height);
}

FrameBufferSource::VPXEncoder::~VPXEncoder() {
    free(mI420Data);
    mI420Data = nullptr;
}

void FrameBufferSource::VPXEncoder::forceIDRFrame() {
    mForceIDRFrame = true;
}

bool FrameBufferSource::VPXEncoder::isForcingIDRFrame() const {
    return mForceIDRFrame;
}

sp<ABuffer> FrameBufferSource::VPXEncoder::encode(
        const void *frame, int64_t timeUs) {
    uint8_t *yPlane = static_cast<uint8_t *>(mI420Data);
    uint8_t *uPlane = yPlane + mSizeY;
    uint8_t *vPlane = uPlane + mSizeUV;

    libyuv::ABGRToI420(
            static_cast<const uint8_t *>(frame),
            mWidth * 4,
            yPlane,
            mWidth,
            uPlane,
            mWidth / 2,
            vPlane,
            mWidth / 2,
            mWidth,
            mHeight);

    vpx_image_t raw_frame;
    vpx_img_wrap(
            &raw_frame,
            VPX_IMG_FMT_I420,
            mWidth,
            mHeight,
            2 /* stride_align */,
            yPlane);

    vpx_enc_frame_flags_t flags = 0;

    if (mForceIDRFrame.exchange(false)) {
        flags |= VPX_EFLAG_FORCE_KF;
    }

    uint32_t frameDuration;

    if (!mFirstFrame) {
        frameDuration = timeUs - mLastTimeUs;
    } else {
        frameDuration = 1000000 / mRefreshRateHz;
        mFirstFrame = false;
    }

    mLastTimeUs = timeUs;

    auto res = vpx_codec_encode(
            mCodecContext.get(),
            &raw_frame,
            timeUs,
            frameDuration,
            flags,
            VPX_DL_REALTIME);

    if (res != VPX_CODEC_OK) {
        LOG(ERROR) << "vpx_codec_encode failed w/ " << res;
        return nullptr;
    }

    vpx_codec_iter_t iter = nullptr;
    const vpx_codec_cx_pkt_t *packet;

    sp<ABuffer> accessUnit;

    while ((packet = vpx_codec_get_cx_data(mCodecContext.get(), &iter)) != nullptr) {
        if (packet->kind == VPX_CODEC_CX_FRAME_PKT) {
            LOG(VERBOSE)
                << "vpx_codec_encode returned packet of size "
                << packet->data.frame.sz;

            if (accessUnit != nullptr) {
                LOG(ERROR)
                    << "vpx_codec_encode returned more than one packet of "
                        "compressed data!";

                return nullptr;
            }

            accessUnit = new ABuffer(packet->data.frame.sz);

            memcpy(accessUnit->data(),
                   packet->data.frame.buf,
                   packet->data.frame.sz);

            accessUnit->meta()->setInt64("timeUs", timeUs);
        } else {
            LOG(INFO)
                << "vpx_codec_encode returned a packet of type "
                << packet->kind;
        }
    }

    return accessUnit;
}

sp<AMessage> FrameBufferSource::VPXEncoder::getFormat() const {
    return mFormat;
}

////////////////////////////////////////////////////////////////////////////////

FrameBufferSource::FrameBufferSource(Format format)
    : mInitCheck(NO_INIT),
      mState(STOPPED),
      mFormat(format),
      mScreenWidth(0),
      mScreenHeight(0),
      mScreenDpi(0),
      mScreenRate(0),
      mOnFrameFn(nullptr) {
    mInitCheck = OK;
}

FrameBufferSource::~FrameBufferSource() {
    stop();
}

status_t FrameBufferSource::initCheck() const {
    return mInitCheck;
}

void FrameBufferSource::setParameters(const sp<AMessage> &params) {
    std::string mime;
    if (params != nullptr && params->findString("mime", &mime)) {
        if (!strcasecmp(mime.c_str(), MEDIA_MIMETYPE_VIDEO_VP8)) {
            mFormat = Format::VP8;
#if ENABLE_H264
        } else if (!strcasecmp(mime.c_str(), MEDIA_MIMETYPE_VIDEO_AVC)) {
            mFormat = Format::H264;
#endif
        } else {
            LOG(WARNING)
                << "Unknown video encoding mime type requested: '"
                << mime
                << "', ignoring.";
        }
    }
}

sp<AMessage> FrameBufferSource::getFormat() const {
    // We're not using the encoder's format (although it will be identical),
    // because the encoder may not have been instantiated just yet.

    sp<AMessage> format = new AMessage;
    format->setString(
            "mime",
            mFormat == Format::H264
                ? MEDIA_MIMETYPE_VIDEO_AVC : MEDIA_MIMETYPE_VIDEO_VP8);

    format->setInt32("width", mScreenWidth);
    format->setInt32("height", mScreenHeight);

    return format;
}

status_t FrameBufferSource::start() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState != STOPPED) {
        return OK;
    }

    switch (mFormat) {
#if ENABLE_H264
        case Format::H264:
        {
            mEncoder.reset(
                    new H264Encoder(mScreenWidth, mScreenHeight, mScreenRate));

            break;
        }
#endif

        case Format::VP8:
        {
            mEncoder.reset(
                    new VPXEncoder(mScreenWidth, mScreenHeight, mScreenRate));

            break;
        }

        default:
            TRESPASS();
    }

    mState = RUNNING;

    return OK;
}

status_t FrameBufferSource::stop() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == STOPPED) {
        return OK;
    }

    mState = STOPPING;

    mState = STOPPED;

    mEncoder.reset();

    return OK;
}

status_t FrameBufferSource::pause() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == PAUSED) {
        return OK;
    }

    if (mState != RUNNING) {
        return INVALID_OPERATION;
    }

    mState = PAUSED;

    LOG(VERBOSE) << "Now paused.";

    return OK;
}

status_t FrameBufferSource::resume() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == RUNNING) {
        return OK;
    }

    if (mState != PAUSED) {
        return INVALID_OPERATION;
    }

    mState = RUNNING;

    LOG(VERBOSE) << "Now running.";

    return OK;
}

bool FrameBufferSource::paused() const {
    return mState == PAUSED;
}

status_t FrameBufferSource::requestIDRFrame() {
    mEncoder->forceIDRFrame();

    return OK;
}

void FrameBufferSource::setScreenParams(const int32_t screenParams[4]) {
    mScreenWidth = screenParams[0];
    mScreenHeight = screenParams[1];
    mScreenDpi = screenParams[2];
    mScreenRate = screenParams[3];
}

void FrameBufferSource::injectFrame(const void *data, size_t size) {
    // Only used in the case of CrosVM operation.
    (void)size;

    std::lock_guard<std::mutex> autoLock(mLock);
    if (/* noone is listening || */ mState != State::RUNNING) {
        return;
    }

    sp<ABuffer> accessUnit = mEncoder->encode(data, ALooper::GetNowUs());

    StreamingSource::onAccessUnit(accessUnit);
}

}  // namespace android

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

#include <source/FrameBufferSource.h>

#include <algorithm>
#include <chrono>

#include <libyuv/convert.h>

#include "host/libs/config/cuttlefish_config.h"

#include "vpx/vpx_encoder.h"
#include "vpx/vpx_codec.h"
#include "vpx/vp8cx.h"

#include <gflags/gflags.h>

#define ENABLE_LOGGING          0

namespace android {

namespace {
    int64_t GetNowUs() {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    }
}

struct FrameBufferSource::Encoder {
    Encoder() = default;
    virtual ~Encoder() = default;

    virtual void forceIDRFrame() = 0;
    virtual bool isForcingIDRFrame() const = 0;

    virtual void storeFrame(const void* frame) = 0;
    virtual std::shared_ptr<SBuffer> encodeStoredFrame(int64_t timeUs) = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct FrameBufferSource::VPXEncoder : public FrameBufferSource::Encoder {
    VPXEncoder(int width, int height, int rateHz);
    ~VPXEncoder() override;

    void forceIDRFrame() override;
    bool isForcingIDRFrame() const override;

    void storeFrame(const void* frame) override;
    std::shared_ptr<SBuffer> encodeStoredFrame(int64_t timeUs) override;

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
    bool mStoredFrame;
    int64_t mLastTimeUs;
    int64_t mFirstTimeUs;
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
      mStoredFrame(false),
      mLastTimeUs(0),
      mFirstTimeUs(0) {

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

void FrameBufferSource::VPXEncoder::storeFrame(const void *frame) {
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
    mStoredFrame = true;
}

std::shared_ptr<SBuffer> FrameBufferSource::VPXEncoder::encodeStoredFrame(
        int64_t timeUs) {
    if (!mStoredFrame) {
        return nullptr;
    }
    vpx_image_t raw_frame;
    vpx_img_wrap(&raw_frame, VPX_IMG_FMT_I420, mWidth, mHeight,
                 2 /* stride_align */,
                 reinterpret_cast<unsigned char *>(mI420Data));

    vpx_enc_frame_flags_t flags = 0;

    if (mForceIDRFrame.exchange(false)) {
        flags |= VPX_EFLAG_FORCE_KF;
    }

    uint32_t frameDuration;

    if (mFirstFrame) {
        mFirstTimeUs = timeUs;
    }
    auto timeStamp = timeUs - mFirstTimeUs;

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
            timeStamp,
            frameDuration,
            flags,
            VPX_DL_REALTIME);

    if (res != VPX_CODEC_OK) {
        LOG(ERROR) << "vpx_codec_encode failed w/ " << res;
        return nullptr;
    }

    vpx_codec_iter_t iter = nullptr;
    const vpx_codec_cx_pkt_t *packet;

    std::shared_ptr<SBuffer> accessUnit;

    while ((packet = vpx_codec_get_cx_data(mCodecContext.get(), &iter)) !=
            nullptr) {
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

            accessUnit.reset(new SBuffer(packet->data.frame.sz));

            memcpy(accessUnit->data(),
                   packet->data.frame.buf,
                   packet->data.frame.sz);

            accessUnit->time_us(timeUs);
        } else {
            LOG(INFO)
                << "vpx_codec_encode returned a packet of type "
                << packet->kind;
        }
    }

    return accessUnit;
}

////////////////////////////////////////////////////////////////////////////////

FrameBufferSource::FrameBufferSource(Format format)
    : mInitCheck(-ENODEV),
      mState(STOPPED),
      mFormat(format),
      mScreenWidth(0),
      mScreenHeight(0),
      mScreenDpi(0),
      mScreenRate(0),
      mNumConsumers(0),
      mOnFrameFn(nullptr) {
    mInitCheck = 0;
}

FrameBufferSource::~FrameBufferSource() {
    stop();
}

int32_t FrameBufferSource::initCheck() const {
    return mInitCheck;
}

int32_t FrameBufferSource::start() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState != STOPPED) {
        return 0;
    }

    switch (mFormat) {
        case Format::VP8:
        {
            mEncoder.reset(
                    new VPXEncoder(mScreenWidth, mScreenHeight, mScreenRate));

            break;
        }

        default:
            LOG(FATAL) << "Should not be here.";
    }

    mState = RUNNING;

    return 0;
}

int32_t FrameBufferSource::stop() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == STOPPED) {
        return 0;
    }

    mState = STOPPING;

    mState = STOPPED;

    mEncoder.reset();

    return 0;
}

int32_t FrameBufferSource::pause() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == PAUSED) {
        return 0;
    }

    if (mState != RUNNING) {
        return -EINVAL;
    }

    mState = PAUSED;

    LOG(VERBOSE) << "Now paused.";

    return 0;
}

int32_t FrameBufferSource::resume() {
    std::lock_guard<std::mutex> autoLock(mLock);

    if (mState == RUNNING) {
        return 0;
    }

    if (mState != PAUSED) {
        return -EINVAL;
    }

    mState = RUNNING;

    LOG(VERBOSE) << "Now running.";

    return 0;
}

bool FrameBufferSource::paused() const {
    return mState == PAUSED;
}

int32_t FrameBufferSource::requestIDRFrame() {
    mEncoder->forceIDRFrame();

    return 0;
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
    if (mState != State::RUNNING) {
        return;
    }

    mEncoder->storeFrame(data);
    // Only encode and forward the frame when there are consumers connected
    if (mNumConsumers) {
        auto accessUnit = mEncoder->encodeStoredFrame(GetNowUs());
        StreamingSource::onAccessUnit(accessUnit);
    }
}

void FrameBufferSource::notifyNewStreamConsumer() {
    std::lock_guard<std::mutex> autoLock(mLock);
    ++mNumConsumers;
    if (mState != State::RUNNING) {
        return;
    }

    mEncoder->forceIDRFrame();
    auto accessUnit = mEncoder->encodeStoredFrame(GetNowUs());
    if (!accessUnit) {
        // nullptr means there isn't a stored frame to encode.
        return;
    }

    StreamingSource::onAccessUnit(accessUnit);
}

void FrameBufferSource::notifyStreamConsumerDisconnected() {
    std::lock_guard<std::mutex> autoLock(mLock);
    --mNumConsumers;
}

}  // namespace android

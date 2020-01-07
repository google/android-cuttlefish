/*
 * Copyright 2018, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <source/StreamingSource.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>

#include <vector>
#include <memory>
#include <thread>

namespace android {

struct CameraSource : public StreamingSource {
    CameraSource();

    CameraSource(const CameraSource &) = delete;
    CameraSource &operator=(const CameraSource &) = delete;

    ~CameraSource() override;

    status_t initCheck() const override;

    sp<AMessage> getFormat() const override;

    status_t start() override;
    status_t stop() override;

    status_t pause() override;
    status_t resume() override;

    bool paused() const override;

    status_t requestIDRFrame() override;

private:
    enum State {
        STOPPING,
        STOPPED,
        RUNNING,
        PAUSED
    };

    status_t mInitCheck;
    State mState;

    std::vector<sp<ABuffer>> mCSD;

    void *mSession;

    std::mutex mLock;

    static void onFrameData(
            void *cookie,
            ssize_t csdIndex,
            int64_t timeUs,
            const void *data,
            size_t size);

    void onFrameData(
            ssize_t csdIndex, int64_t timeUs, const void *data, size_t size);

    sp<ABuffer> prependCSD(const sp<ABuffer> &accessUnit) const;
};

}  // namespace android

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CameraSessionCallback)(
        void *cookie, ssize_t csdIndex, int64_t timeUs, const void *data, size_t size);

void *createCameraSession(CameraSessionCallback cb, void *cookie);
void startCameraSession(void *session);
void stopCameraSession(void *session);
void pauseCameraSession(void *session);
void resumeCameraSession(void *session);
void destroyCameraSession(void *session);

#ifdef __cplusplus
}
#endif




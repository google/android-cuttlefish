/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef MINIFENCE_H
#define MINIFENCE_H

#include <utils/RefBase.h>

namespace android {

/* MiniFence is a minimal re-implementation of Fence from libui. It exists to
 * avoid linking the HWC2on1Adapter to libui and satisfy Treble requirements.
 */
class MiniFence : public LightRefBase<MiniFence> {
public:
    static const sp<MiniFence> NO_FENCE;

    // Construct a new MiniFence object with an invalid file descriptor.
    MiniFence();

    // Construct a new MiniFence object to manage a given fence file descriptor.
    // When the new MiniFence object is destructed the file descriptor will be
    // closed.
    explicit MiniFence(int fenceFd);

    // Not copyable or movable.
    MiniFence(const MiniFence& rhs) = delete;
    MiniFence& operator=(const MiniFence& rhs) = delete;
    MiniFence(MiniFence&& rhs) = delete;
    MiniFence& operator=(MiniFence&& rhs) = delete;

    // Return a duplicate of the fence file descriptor. The caller is
    // responsible for closing the returned file descriptor. On error, -1 will
    // be returned and errno will indicate the problem.
    int dup() const;

private:
    // Only allow instantiation using ref counting.
    friend class LightRefBase<MiniFence>;
    ~MiniFence();

    int mFenceFd;

};
}
#endif //MINIFENCE_H

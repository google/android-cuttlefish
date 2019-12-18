#include <source/VSOCTouchSink.h>

#include <media/stagefright/foundation/ABuffer.h>

#include "host/libs/config/cuttlefish_config.h"

namespace android {

VSOCTouchSink::VSOCTouchSink()
    : mInputEventsRegionView(
            InputEventsRegionView::GetInstance(vsoc::GetDomain().c_str())) {
}

void VSOCTouchSink::onAccessUnit(const sp<ABuffer> &accessUnit) {
    const int32_t *data =
        reinterpret_cast<const int32_t *>(accessUnit->data());

    if (accessUnit->size() == 3 * sizeof(int32_t)) {
        // Legacy: Single Touch Emulation.

        bool down = data[0] != 0;
        int x = data[1];
        int y = data[2];

        LOG(VERBOSE)
            << "Received touch (down="
            << down
            << ", x="
            << x
            << ", y="
            << y;

        mInputEventsRegionView->HandleSingleTouchEvent(down, x, y);
        return;
    }

    CHECK_EQ(accessUnit->size(), 5 * sizeof(int32_t));

    int id = data[0];
    bool initialDown = data[1] != 0;
    int x = data[2];
    int y = data[3];
    int slot = data[4];

    LOG(VERBOSE)
        << "Received touch (id="
        << id
        << ", initialDown="
        << initialDown
        << ", x="
        << x
        << ", y="
        << y
        << ", slot="
        << slot;

    mInputEventsRegionView->HandleMultiTouchEvent(id, initialDown, x, y, slot);
}

}  // namespace android


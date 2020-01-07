#pragma once

#include <source/StreamingSink.h>

#include "common/vsoc/lib/input_events_region_view.h"

namespace android {

struct VSOCTouchSink : public StreamingSink {
    VSOCTouchSink();

    void onAccessUnit(const sp<ABuffer> &accessUnit) override;

private:
    using InputEventsRegionView = vsoc::input_events::InputEventsRegionView;

    InputEventsRegionView *mInputEventsRegionView;
};

}  // namespace android


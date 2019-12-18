#include <source/VideoSink.h>

#include <media/stagefright/avc_utils.h>
#include <media/stagefright/Utils.h>
#include <media/stagefright/MetaData.h>

namespace android {

VideoSink::VideoSink()
    : mRenderer(new DirectRenderer_iOS),
      mFirstAccessUnit(true) {
}

void VideoSink::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatAccessUnit:
        {
            sp<ABuffer> accessUnit;
            CHECK(msg->findBuffer("accessUnit", &accessUnit));

            if (mFirstAccessUnit) {
                mFirstAccessUnit = false;

                sp<MetaData> meta = MakeAVCCodecSpecificData(accessUnit);
                CHECK(meta != nullptr);

                sp<AMessage> format;
                CHECK_EQ(OK, convertMetaDataToMessage(meta, &format));

                mRenderer->setFormat(0, format);
            }

            mRenderer->queueAccessUnit(0, accessUnit);
            break;
        }

        default:
            TRESPASS();
    }
}

}  // namespace android


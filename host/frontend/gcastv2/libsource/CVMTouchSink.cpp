#include <source/CVMTouchSink.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>

#include <sys/socket.h>

namespace android {

CVMTouchSink::CVMTouchSink(std::shared_ptr<RunLoop> runLoop, int serverFd)
    : mRunLoop(runLoop),
      mServerFd(serverFd),
      mClientFd(-1),
      mSendPending(false) {
    if (mServerFd >= 0) {
        makeFdNonblocking(mServerFd);
    }
}

CVMTouchSink::~CVMTouchSink() {
    if (mClientFd >= 0) {
        mRunLoop->cancelSocket(mClientFd);

        close(mClientFd);
        mClientFd = -1;
    }

    if (mServerFd >= 0) {
        mRunLoop->cancelSocket(mServerFd);

        close(mServerFd);
        mServerFd = -1;
    }
}

void CVMTouchSink::start() {
    if (mServerFd < 0) {
        return;
    }

    mRunLoop->postSocketRecv(
            mServerFd,
            makeSafeCallback(this, &CVMTouchSink::onServerConnection));
}

void CVMTouchSink::onServerConnection() {
    int s = accept(mServerFd, nullptr, nullptr);

    if (s >= 0) {
        if (mClientFd >= 0) {
            LOG(INFO) << "Rejecting client, we already have one.";

            // We already have a client.
            close(s);
            s = -1;
        } else {
            LOG(INFO) << "Accepted client socket " << s << ".";

            makeFdNonblocking(s);

            mClientFd = s;
        }
    }

    mRunLoop->postSocketRecv(
            mServerFd,
            makeSafeCallback(this, &CVMTouchSink::onServerConnection));
}

static void AddInputEvent(
        std::vector<input_event> *events,
        uint16_t type,
        uint16_t code,
        int32_t value) {
    input_event ev;
    ev.type = type;
    ev.code = code;
    ev.value = value;

    events->push_back(ev);
}

void CVMTouchSink::onAccessUnit(const sp<ABuffer> &accessUnit) {
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

        std::vector<input_event> events;
        AddInputEvent(&events, EV_ABS, ABS_X, x);
        AddInputEvent(&events, EV_ABS, ABS_Y, y);
        AddInputEvent(&events, EV_KEY, BTN_TOUCH, down);
        AddInputEvent(&events, EV_SYN, 0, 0);

        sendEvents(events);
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

    std::vector<input_event> events;

#if 0
    AddInputEvent(&events, EV_ABS, ABS_MT_SLOT, slot);

    if (id < 0 || initialDown) {
        AddInputEvent(&events, EV_ABS, ABS_MT_TRACKING_ID, id);
        AddInputEvent(&events, EV_KEY, BTN_TOUCH, initialDown);
    }

    if (id >= 0) {
        AddInputEvent(&events, EV_ABS, ABS_MT_POSITION_X, x);
        AddInputEvent(&events, EV_ABS, ABS_MT_POSITION_Y, y);
    }

    AddInputEvent(&events, EV_SYN, SYN_REPORT, 0);
#else
    AddInputEvent(&events, EV_ABS, ABS_X, x);
    AddInputEvent(&events, EV_ABS, ABS_Y, y);
    AddInputEvent(&events, EV_KEY, BTN_TOUCH, id >= 0);
    AddInputEvent(&events, EV_SYN, 0, 0);
#endif

    sendEvents(events);
}

void CVMTouchSink::sendEvents(const std::vector<input_event> &events) {
    if (events.empty()) {
        return;
    }

    std::lock_guard autoLock(mLock);

    if (mClientFd < 0) {
        return;
    }

    auto size = events.size() * sizeof(input_event);

    size_t offset = mOutBuffer.size();
    mOutBuffer.resize(offset + size);
    memcpy(mOutBuffer.data() + offset, events.data(), size);

    if (!mSendPending) {
        mSendPending = true;

        mRunLoop->postSocketSend(
                mClientFd,
                makeSafeCallback(this, &CVMTouchSink::onSocketSend));
    }
}

void CVMTouchSink::onSocketSend() {
    std::lock_guard autoLock(mLock);

    CHECK(mSendPending);
    mSendPending = false;

    if (mClientFd < 0) {
        return;
    }

    ssize_t n;
    while (!mOutBuffer.empty()) {
        do {
            n = ::send(mClientFd, mOutBuffer.data(), mOutBuffer.size(), 0);
        } while (n < 0 && errno == EINTR);

        if (n <= 0) {
            break;
        }

        mOutBuffer.erase(mOutBuffer.begin(), mOutBuffer.begin() + n);
    }

    if ((n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || n == 0) {
        LOG(ERROR) << "Client is gone.";

        // Client is gone.
        mRunLoop->cancelSocket(mClientFd);

        close(mClientFd);
        mClientFd = -1;
        return;
    }

    if (!mOutBuffer.empty()) {
        mSendPending = true;
        mRunLoop->postSocketSend(
                mClientFd,
                makeSafeCallback(this, &CVMTouchSink::onSocketSend));
    }
}

}  // namespace android


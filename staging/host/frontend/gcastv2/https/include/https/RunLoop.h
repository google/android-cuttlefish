#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/select.h>

struct MainRunLoop;

struct RunLoop {
    explicit RunLoop(std::string_view name);
    ~RunLoop();

    static std::shared_ptr<RunLoop> main();

    // For public use on the main RunLoop only.
    void stop();
    void run();

    RunLoop(const RunLoop &) = delete;
    RunLoop &operator=(const RunLoop &) = delete;

    typedef std::function<void()> AsyncFunction;
    typedef int32_t Token;

    Token post(AsyncFunction fn);

    Token postWithDelay(
            std::chrono::steady_clock::duration delay, AsyncFunction fn);

    // Returns true iff matching event was cancelled.
    bool cancelToken(Token token);

    void postSocketRecv(int sock, AsyncFunction fn);
    void postSocketSend(int sock, AsyncFunction fn);
    void cancelSocket(int sock);

    bool isCurrentThread() const;

private:
    friend struct MainRunLoop;

    struct QueueElem {
        std::optional<std::chrono::steady_clock::time_point> mWhen;
        AsyncFunction mFn;
        Token mToken;

        bool operator<=(const QueueElem &other) const;
    };

    struct SocketCallbacks {
        AsyncFunction mRecvFn;
        AsyncFunction mSendFn;
        size_t mPollFdIndex;
    };

    enum class InfoType {
        RECV,
        SEND,
        CANCEL,
    };

    struct AddSocketCallbackInfo {
        int mSock;
        InfoType mType;
        AsyncFunction mFn;
    };

    std::string mName;

    int mControlFds[2];

    std::mutex mLock;
    std::deque<QueueElem> mQueue;
    std::vector<AddSocketCallbackInfo> mAddInfos;

    std::atomic<bool> mDone;
    std::thread mThread;
    pthread_t mPThread;

    std::atomic<Token> mNextToken;

    explicit RunLoop();  // constructor for the main RunLoop.

    void interrupt();
    void insert(const QueueElem &elem);
    void addPollFd_l(int sock);
};


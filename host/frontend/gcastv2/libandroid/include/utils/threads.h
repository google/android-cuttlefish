#ifndef ANDROID_THREADS_H_

#define ANDROID_THREADS_H_

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/foundation/ADebug.h>

#include <sys/time.h>

#include <utils/Errors.h>
#include <utils/RefBase.h>

#include <pthread.h>

namespace android {

struct Thread : public RefBase {
    Thread()
        : mThread(0)
    {
    }

    status_t run(const char *name) {
        if (mThread != 0) {
            return INVALID_OPERATION;
        }

        mName = name;

        mExitRequested = false;
        int res = pthread_create(&mThread, NULL, &Thread::ThreadWrapper, this);

        if (res != 0) {
            mThread = 0;

            return -errno;
        }

        return OK;
    }

    void requestExit() { mExitRequested = true; }

    void requestExitAndWait() {
        requestExit();
        void *dummy;
        pthread_join(mThread, &dummy);
        mThread = 0;
    }

protected:
    virtual bool threadLoop() = 0;

    ~Thread() {
        if (mThread) {
            requestExitAndWait();

            CHECK(!mThread);
        }
    }

private:
    pthread_t mThread;
    volatile bool mExitRequested;
    std::string mName;

    static void *ThreadWrapper(void *param) {
        Thread *me = static_cast<Thread *>(param);

        while (!me->mExitRequested) {
            if (!me->threadLoop()) {
                break;
            }
        }

        return NULL;
    }

    DISALLOW_EVIL_CONSTRUCTORS(Thread);
};

struct Mutex {
    Mutex() {
        CHECK_EQ(pthread_mutex_init(&mMutex, NULL), 0);
    }

    ~Mutex() {
        CHECK_EQ(pthread_mutex_destroy(&mMutex), 0);
    }

    void lock() {
        CHECK_EQ(pthread_mutex_lock(&mMutex), 0);
    }

    void unlock() {
        CHECK_EQ(pthread_mutex_unlock(&mMutex), 0);
    }

    struct Autolock;

private:
    friend struct Condition;

    pthread_mutex_t mMutex;

    DISALLOW_EVIL_CONSTRUCTORS(Mutex);
};

struct Mutex::Autolock {
    Autolock(Mutex &mutex)
        : mMutex(mutex) {
        mMutex.lock();
    }

    ~Autolock() {
        mMutex.unlock();
    }

private:
    Mutex &mMutex;

    DISALLOW_EVIL_CONSTRUCTORS(Autolock);
};

struct Condition {
    Condition() {
        CHECK_EQ(pthread_cond_init(&mCond, NULL), 0);
    }

    ~Condition() {
        CHECK_EQ(pthread_cond_destroy(&mCond), 0);
    }

    void signal() {
        CHECK_EQ(pthread_cond_signal(&mCond), 0);
    }

    void broadcast() {
        CHECK_EQ(pthread_cond_broadcast(&mCond), 0);
    }

    status_t wait(Mutex &mutex) {
        int res = pthread_cond_wait(&mCond, &mutex.mMutex);

        return res == 0 ? OK : -errno;
    }

    status_t waitRelative(Mutex &mutex, int64_t nsecs) {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        struct timespec abstime;
        abstime.tv_sec = tv.tv_sec;
        abstime.tv_nsec = tv.tv_usec * 1000ll + nsecs;

        int res = pthread_cond_timedwait(&mCond, &mutex.mMutex, &abstime);

        if (res == 0) {
            return OK;
        }

        return -errno;
    }

private:
    pthread_cond_t mCond;

    DISALLOW_EVIL_CONSTRUCTORS(Condition);
};

}  // namespace android

#endif  // ANDROID_THREADS_H_

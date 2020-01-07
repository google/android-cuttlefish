#ifndef ANDROID_REFBASE_H_

#define ANDROID_REFBASE_H_

#include <stdlib.h>

#include <media/stagefright/foundation/ABase.h>
#include <media/stagefright/foundation/ADebug.h>

#include <atomic>
#include <list>

namespace android {

struct RefBase;

// XXX Warning: The weak ref handling is NOT threadsafe yet.
struct WeakList {
    WeakList(RefBase *obj)
        : mObject(obj) {
    }

    void add(void *id) {
        mRefs.push_back(id);
    }

    void remove(void *id) {
        auto it = mRefs.begin();
        while (it != mRefs.end() && *it != id) {
            ++it;
        }
        CHECK(it != mRefs.end());
        mRefs.erase(it);

        if (mRefs.empty() && mObject == NULL) {
            delete this;
        }
    }

    RefBase *promote() const;

private:
    friend struct RefBase;

    RefBase *mObject;
    std::list<void *> mRefs;

    ~WeakList() {
        CHECK(mRefs.empty());
    }

    void objectDied() {
        mObject = NULL;
        if (mRefs.empty()) {
            delete this;
        }
    }

    DISALLOW_EVIL_CONSTRUCTORS(WeakList);
};

struct RefBase {
    RefBase()
        : mNumRefs(0),
          mWeakList(NULL) {
    }

    void incStrong(const void *) {
        ++mNumRefs;
    }

    void decStrong(const void *) {
        if (--mNumRefs == 0) {
            if (mWeakList != NULL) {
                mWeakList->objectDied();
                mWeakList = NULL;
            }

            delete this;
        }
    }

    WeakList *getWeakRefs() {
        if (mWeakList == NULL) {
            mWeakList = new WeakList(this);
        }

        return mWeakList;
    }

protected:
    virtual ~RefBase() {}

private:
    std::atomic<int32_t> mNumRefs;
    WeakList *mWeakList;

    DISALLOW_EVIL_CONSTRUCTORS(RefBase);
};

template<class T>
struct sp {
    sp() : mObj(NULL) {}

    sp(T *obj)
        : mObj(obj) {
        if (mObj) { mObj->incStrong(this); }
    }

    sp(const sp<T> &other)
        : mObj(other.mObj) {
        if (mObj) { mObj->incStrong(this); }
    }

    template<class U>
    sp(const sp<U> &other)
        : mObj(other.mObj) {
        if (mObj) { mObj->incStrong(this); }
    }

    ~sp() {
        if (mObj) { mObj->decStrong(this); }
    }

    T &operator*() const { return *mObj; }
    T *operator->() const { return mObj; }
    T *get() const { return mObj; }

    sp<T> &operator=(T *obj) {
        if (obj) { obj->incStrong(this); }
        if (mObj) { mObj->decStrong(this); }
        mObj = obj;

        return *this;
    }

    sp<T> &operator=(const sp<T> &other) {
        return (*this) = other.mObj;
    }

    template<class U>
    sp<T> &operator=(const sp<U> &other) {
        return (*this) = other.mObj;
    }

    void clear() {
        if (mObj) { mObj->decStrong(this); mObj = NULL; }
    }

    bool operator==(const sp<T> &other) const {
        return mObj == other.mObj;
    }

    bool operator!=(const sp<T> &other) const {
        return mObj != other.mObj;
    }

    explicit operator bool() const {
        return mObj != nullptr;
    }

private:
    template<typename Y> friend struct sp;

    T *mObj;
};

template<class T>
struct wp {
    wp() : mWeakList(NULL) {
    }

    wp(T *obj) {
        if (obj != NULL) {
          mWeakList = obj->getWeakRefs();
          mWeakList->add(this);
        } else {
          mWeakList = NULL;
        }
    }

    wp(const wp<T> &other)
        : mWeakList(other.mWeakList) {
        if (mWeakList != NULL) {
            mWeakList->add(this);
        }
    }

    wp<T> &operator=(const wp<T> &other) {
        if (mWeakList != other.mWeakList) {
            if (other.mWeakList != NULL) {
                other.mWeakList->add(this);
            }
            if (mWeakList != NULL) {
                mWeakList->remove(this);
            }
            mWeakList = other.mWeakList;
        }

        return *this;
    }

    ~wp() {
        clear();
    }

    void clear() {
        if (mWeakList != NULL) {
            mWeakList->remove(this);
            mWeakList = NULL;
        }
    }

    sp<T> promote() const {
        if (mWeakList == NULL) {
            return NULL;
        }

        sp<T> result = (T *)mWeakList->promote();

        if (result != NULL) {
            result->decStrong(this);
        }

        return result;
    }

private:
    WeakList *mWeakList;
};

}  // namespace android

#endif  // ANDROID_REFBASE_H_

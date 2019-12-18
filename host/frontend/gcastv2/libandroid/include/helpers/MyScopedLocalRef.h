#pragma once

#include <jni.h>

template<class T>
struct MyScopedLocalRef {
    explicit MyScopedLocalRef()
        : mEnv(nullptr),
          mObj(nullptr) {
    }

    explicit MyScopedLocalRef(JNIEnv *env, T obj)
        : mEnv(env),
          mObj(obj) {
    }

    MyScopedLocalRef(const MyScopedLocalRef<T> &other) = delete;
    MyScopedLocalRef &operator=(const MyScopedLocalRef<T> &other) = delete;

    void setTo(JNIEnv *env, T obj) {
        if (obj != mObj) {
            clear();

            mEnv = env;
            mObj = obj;
        }
    }

    void clear() {
        if (mObj) {
            mEnv->DeleteLocalRef(mObj);
            mObj = nullptr;
        }

        mEnv = nullptr;
    }

    ~MyScopedLocalRef() {
        clear();
    }

    T get() const {
        return mObj;
    }

private:
    JNIEnv *mEnv;
    T mObj;
};

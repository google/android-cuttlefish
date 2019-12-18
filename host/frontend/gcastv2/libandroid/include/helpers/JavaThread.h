#pragma once

#include <helpers/MyAndroidRuntime.h>

#include <thread>

namespace android {

void javaAttachThread();
void javaDetachThread();

template<class Function, class... Args>
std::thread createJavaThread(Function &&f, Args&&... args) {
    return std::thread([f, args...]{
        javaAttachThread();
        f(args...);
        javaDetachThread();
    });
}

}  // namespace android


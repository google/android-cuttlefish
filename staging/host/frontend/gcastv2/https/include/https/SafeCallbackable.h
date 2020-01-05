#pragma once

#include <memory>

template<typename T>
std::function<void()> makeSafeCallback(T *obj, std::function<void(T *)> f) {
    auto weak_me = std::weak_ptr<T>(obj->shared_from_this());
    return [f, weak_me]{
        auto me = weak_me.lock();
        if (me) {
            f(me.get());
        }
    };
}

template<typename T, typename... Params>
std::function<void()> makeSafeCallback(
        T *obj, void (T::*f)(const Params&...), const Params&... params) {
    return makeSafeCallback<T>(
            obj,
            [f, params...](T *me) {
                (me->*f)(params...);
            });
}

template<typename T, typename... Params>
std::function<void()> makeSafeCallback(
        T *obj, void (T::*f)(Params...), const Params&... params) {
    return makeSafeCallback<T>(
            obj,
            [f, params...](T *me) {
                (me->*f)(params...);
            });
}

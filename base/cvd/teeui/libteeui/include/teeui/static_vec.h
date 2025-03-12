/*
 *
 * Copyright 2019, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TEEUI_STATIC_VEC_H_
#define TEEUI_STATIC_VEC_H_

#include <stddef.h>  // for size_t

#ifdef TEEUI_USE_STD_VECTOR
#include <vector>
#endif

namespace teeui {

/**
 * teeui::static_vec leads a double life.
 *
 * When compiling with TEEUI_USE_STD_VECTOR it is just an alias for std::vector. HAL services using
 * this library must use this option for safe handling of message buffers.
 *
 * When compiling without TEEUI_USE_STD_VECTOR this class works more like a span that does not
 * actually own the buffer if wraps. This is the behavior expected by generic_operation.h, which
 * is used inside a heap-less implementation of a confirmationui trusted app.
 */
#ifndef TEEUI_USE_STD_VECTOR
template <typename T> class static_vec {
  private:
    T* data_;
    size_t size_;

  public:
    static_vec() : data_(nullptr), size_(0) {}
    static_vec(T* begin, T* end) : data_(begin), size_(end - begin) {}
    template <size_t s> static_vec(T (&arr)[s]) : data_(&arr[0]), size_(s) {}
    static_vec(const static_vec&) = default;
    static_vec(static_vec&&) = default;
    static_vec& operator=(const static_vec&) = default;
    static_vec& operator=(static_vec&&) = default;

    T* data() { return data_; }
    const T* data() const { return data_; }
    size_t size() const { return size_; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }
};
#else
template <typename T> using static_vec = std::vector<T>;
#endif

}  // namespace teeui

#endif  // TEEUI_STATIC_VEC_H_

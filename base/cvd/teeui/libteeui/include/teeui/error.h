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

#ifndef LIBTEEUI_INCLUDE_TEEUI_ERROR_H_
#define LIBTEEUI_INCLUDE_TEEUI_ERROR_H_

#include "log.h"
#include <stdint.h>

namespace teeui {

class Error {
  public:
    enum error_e : uint32_t {
        OK,
        NotInitialized,
        FaceNotLoaded,
        CharSizeNotSet,
        GlyphNotLoaded,
        GlyphNotRendered,
        GlyphNotExtracted,
        UnsupportedPixelFormat,
        OutOfBoundsDrawing,
        BBoxComputation,
        OutOfMemory,
        Localization,
    };

    constexpr Error() noexcept : v_(OK) {}
    constexpr Error(error_e v) noexcept : v_(v) {}
    constexpr Error(Error&&) noexcept = default;
    constexpr Error(const Error&) noexcept = default;

    Error& operator=(error_e v) {
        v_ = v;
        return *this;
    }
    Error& operator=(const Error& other) {
        v_ = other.v_;
        return *this;
    }

    /**
     * Evaluates to true if this represents an error condition.
     * Consider a function Error foo(), use the following pattern to handle the error.
     * if (auto error = foo()) {  <handle error here> }
     */
    operator bool() const { return v_ != OK; }

    Error operator||(const Error& rhs) const { return *this ? *this : rhs; }

    inline bool operator==(error_e v) const { return v_ == v; };
    inline bool operator!=(error_e v) const { return v_ != v; };

    constexpr error_e code() const { return v_; }

  private:
    error_e v_;
};

#ifdef TEEUI_DO_LOG_DEBUG
// keep this in the header, so the test can switch it on without switching it on in the static
// library
[[maybe_unused]] static std::ostream& operator<<(std::ostream& out, Error e) {
    switch (e.code()) {
    case Error::OK:
        return out << "teeui::Error::OK";
    case Error::NotInitialized:
        return out << "teeui::Error::NotInitialized";
    case Error::FaceNotLoaded:
        return out << "teeui::Error::FaceNotLoaded";
    case Error::CharSizeNotSet:
        return out << "teeui::Error::CharSizeNotSet";
    case Error::GlyphNotLoaded:
        return out << "teeui::Error::GlyphNotLoaded";
    case Error::GlyphNotRendered:
        return out << "teeui::Error::GlyphNotRendered";
    case Error::GlyphNotExtracted:
        return out << "teeui::Error::GlyphNotExtracted";
    case Error::UnsupportedPixelFormat:
        return out << "teeui::Error::UnsupportedPixelFormat";
    case Error::OutOfBoundsDrawing:
        return out << "teeui::Error::OutOfBoundsDrawing";
    case Error::BBoxComputation:
        return out << "teeui::Error::BBoxComputation";
    case Error::OutOfMemory:
        return out << "teeui::Error::OutOfMemory";
    default:
        return out << "Invalid teeui::Error Code";
    }
}
#endif

}  // namespace teeui

#endif  // LIBTEEUI_INCLUDE_TEEUI_ERROR_H_

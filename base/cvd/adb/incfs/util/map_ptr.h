/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android-base/logging.h>
#include <android-base/macros.h>
#include <android-base/off64_t.h>

#include <atomic>
#include <iterator>
#include <memory>
#include <shared_mutex>
#include <type_traits>
#include <vector>

#ifdef __ANDROID__
#include <linux/incrementalfs.h>
#endif

namespace android {

class FileMap;

namespace incfs {

// Controls whether not verifying the presence of data before de-referencing the pointer aborts
// program execution.
#define LIBINCFS_MAP_PTR_DEBUG false
#if LIBINCFS_MAP_PTR_DEBUG
#define LIBINCFS_MAP_PTR_DEBUG_CODE(x) x
#else
#define LIBINCFS_MAP_PTR_DEBUG_CODE(x)
#endif

template <typename T, bool Verified = false>
struct map_ptr;

// This class represents a memory-mapped, read-only file that may exist on an IncFs file system.
//
// Files stored on IncFs may not be fully present. This class is able to return a smart pointer
// (map_ptr<T>) that is able to verify whether the contents of the pointer are fully present on
// IncFs.
//
// This always uses MAP_SHARED.
class IncFsFileMap final {
public:
    IncFsFileMap() noexcept;
    IncFsFileMap(IncFsFileMap&&) noexcept;
    IncFsFileMap& operator =(IncFsFileMap&&) noexcept;
    ~IncFsFileMap() noexcept;

    // Initializes the map. Does not take ownership of the file descriptor.
    // Returns whether or not the file was able to be memory-mapped.
    bool Create(int fd, off64_t offset, size_t length, const char* file_name);

    // Same thing, but allows verification to be disabled when `verify` is `false`, and enabled when
    // `verify` is true and the file resides on IncFs.
    bool Create(int fd, off64_t offset, size_t length, const char* file_name, bool verify);

    // Same thing, but allows verification to be disabled when `verify` is `false`, and enabled when
    // `verify` is true regardless of whether the file resides on IncFs (used for benchmarks and
    // testing).
    bool CreateForceVerification(int fd, off64_t offset, size_t length, const char* file_name,
                                 bool verify);

    template <typename T = void>
    map_ptr<T> data() const {
        return map_ptr<T>(verification_enabled_ ? this : nullptr,
                          reinterpret_cast<const T*>(unsafe_data()));
    }

    const void* unsafe_data() const;
    size_t length() const;
    off64_t offset() const;
    const char* file_name() const;

public:
    // Returns whether the data range is entirely present on IncFs.
    bool Verify(const uint8_t* const& data_start, const uint8_t* const& data_end,
                const uint8_t** prev_verified_block) const;

private:
    DISALLOW_COPY_AND_ASSIGN(IncFsFileMap);

    using bucket_t = uint8_t;
    static constexpr size_t kBucketBits = sizeof(bucket_t) * 8U;

    // File descriptor of the memory-mapped file (not owned).
    int fd_ = -1;
    bool verification_enabled_ = false;
    size_t start_block_offset_ = 0;
    const uint8_t* start_block_ptr_ = nullptr;

    std::unique_ptr<android::FileMap> map_;

    // Bitwise cache for storing whether a block has already been verified. This cache relies on
    // IncFs not deleting blocks of a file that is currently memory mapped.
    mutable std::vector<std::atomic<bucket_t>> loaded_blocks_;

    template <typename, bool>
    friend struct map_ptr;
};

// Variant of map_ptr that statically guarantees that the pointed to data is fully present and
// reading data will not result in IncFs raising a SIGBUS.
template <typename T>
using verified_map_ptr = map_ptr<T, true>;

// Smart pointer that is able to verify whether the contents of the pointer are fully present on
// the file system before using the pointer. Files residing on IncFs may not be fully present.
//
// Before attempting to use the data represented by the smart pointer, the caller should always use
// the bool operator to verify the presence of the data. The bool operator is not thread-safe. If
// this pointer must be used in multiple threads concurrently, use verified_map_ptr instead.
//
// map_ptr created from raw pointers have less overhead than when created from IncFsFileMap.
template <typename T, bool Verified>
struct map_ptr final {
private:
    friend class IncFsFileMap;

    // To access internals of map_ptr with a different type
    template <typename, bool>
    friend struct map_ptr;

    template <typename T1>
    using IsVoid = typename std::enable_if_t<std::is_void<T1>::value, int>;

    template <typename T1>
    using NotVoid = typename std::enable_if_t<!std::is_void<T1>::value, int>;

    template <bool V>
    using IsVerified = typename std::enable_if_t<V, int>;

    template <bool V>
    using IsUnverified = typename std::enable_if_t<!V, int>;

public:
    class const_iterator final {
    public:
        friend struct map_ptr<T, Verified>;
        using iterator_category = std::random_access_iterator_tag;
        using value_type = const map_ptr<T>;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;

        const_iterator() = default;
        const_iterator(const const_iterator& it) = default;

        bool operator==(const const_iterator& other) const { return safe_ptr_ == other.safe_ptr_; }
        bool operator!=(const const_iterator& other) const { return safe_ptr_ != other.safe_ptr_; }
        std::ptrdiff_t operator-(const const_iterator& other) const {
            return safe_ptr_ - other.safe_ptr_;
        }

        const_iterator operator+(int n) const {
            const_iterator other = *this;
            other += n;
            return other;
        }

        reference operator*() const { return safe_ptr_; }

        const const_iterator& operator++() {
            safe_ptr_++;
            return *this;
        }

        const_iterator& operator+=(int n) {
            safe_ptr_ = safe_ptr_ + n;
            return *this;
        }

        const const_iterator operator++(int) {
            const_iterator temp(*this);
            safe_ptr_++;
            return temp;
        }

    private:
        explicit const_iterator(const map_ptr<T>& ptr) : safe_ptr_(ptr) {}
        map_ptr<T> safe_ptr_;
    };

    // Default constructor
    map_ptr() = default;

    // Implicit conversion from raw pointer
    map_ptr(const T* ptr) : map_ptr(nullptr, ptr, nullptr) {}

    // Copy constructor
    map_ptr(const map_ptr& other) = default;

    // Implicit copy conversion from verified to unverified map_ptr<T>
    template <bool V2, bool V1 = Verified, IsUnverified<V1> = 0, IsVerified<V2> = 0>
    map_ptr(const map_ptr<T, V2>& other) : map_ptr(other.map_, other.ptr_, other.verified_block_) {}

    // Move constructor
    map_ptr(map_ptr&& other) noexcept = default;

    // Implicit move conversion from verified to unverified map_ptr<T>
    template <bool V2, bool V1 = Verified, IsUnverified<V1> = 0, IsVerified<V2> = 0>
    map_ptr(map_ptr&& other) : map_ptr(other.map_, other.ptr_, other.verified_block_) {}

    // Implicit conversion to unverified map_ptr<void>
    template <typename U, bool V2, typename T1 = T, bool V1 = Verified, IsVoid<T1> = 0,
              NotVoid<U> = 0, IsUnverified<V1> = 0>
    map_ptr(const map_ptr<U, V2>& other)
          : map_ptr(other.map_, reinterpret_cast<const void*>(other.ptr_), other.verified_block_) {}

    // Implicit conversion from regular raw pointer
    map_ptr& operator=(const T* ptr) {
        ptr_ = ptr;
        map_ = nullptr;
        verified_block_ = nullptr;
        LIBINCFS_MAP_PTR_DEBUG_CODE(verified_ = Verified);
        return *this;
    }

    // Copy assignment operator
    map_ptr& operator=(const map_ptr& other) = default;

    // Copy assignment operator
    template <bool V2, bool V1 = Verified, IsUnverified<V1> = 0, IsVerified<V2> = 0>
    map_ptr& operator=(const map_ptr<T, V2>& other) {
        ptr_ = other.ptr_;
        map_ = other.map_;
        verified_block_ = other.verified_block_;
        LIBINCFS_MAP_PTR_DEBUG_CODE(verified_ = other.verified_);
        return *this;
    }

    template <bool V2>
    bool operator==(const map_ptr<T, V2>& other) const {
        return ptr_ == other.ptr_;
    }

    template <bool V2>
    bool operator!=(const map_ptr<T, V2>& other) const {
        return ptr_ != other.ptr_;
    }

    template <bool V2>
    bool operator<(const map_ptr<T, V2>& other) const {
        return ptr_ < other.ptr_;
    }

    template <bool V2>
    std::ptrdiff_t operator-(const map_ptr<T, V2>& other) const {
        return ptr_ - other.ptr_;
    }

    template <typename U>
    map_ptr<U> convert() const {
        return map_ptr<U>(map_, reinterpret_cast<const U*>(ptr_), verified_block_);
    }

    // Retrieves a map_ptr<T> offset from an original map_ptr<U> by the specified number of `offset`
    // bytes.
    map_ptr<T> offset(std::ptrdiff_t offset) const {
        return map_ptr<T>(map_,
                          reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(ptr_) +
                                                     offset),
                          verified_block_);
    }

    // Returns a raw pointer to the value of this pointer.
    const T* unsafe_ptr() const { return ptr_; }

    // Start T == void methods

    template <typename T1 = T, IsVoid<T1> = 0>
    operator bool() const {
        return ptr_ != nullptr;
    }

    // End T == void methods
    // Start T != void methods

    template <typename T1 = T, NotVoid<T1> = 0, bool V1 = Verified, IsUnverified<V1> = 0>
    operator bool() const {
        return verify();
    }

    template <typename T1 = T, NotVoid<T1> = 0, bool V1 = Verified, IsVerified<V1> = 0>
    operator bool() const {
        return ptr_ != nullptr;
    }

    template <typename T1 = T, NotVoid<T1> = 0>
    const_iterator iterator() const {
        return const_iterator(*this);
    }

    template <typename T1 = T, NotVoid<T1> = 0>
    const map_ptr<T1>& operator++() {
        LIBINCFS_MAP_PTR_DEBUG_CODE(verified_ = false);
        ++ptr_;
        return *this;
    }

    template <typename T1 = T, NotVoid<T1> = 0>
    const map_ptr<T1> operator++(int) {
        map_ptr<T1> temp = *this;
        LIBINCFS_MAP_PTR_DEBUG_CODE(verified_ = false);
        ++ptr_;
        return temp;
    }

    template <typename S, typename T1 = T, NotVoid<T1> = 0>
    map_ptr<T1> operator+(const S n) const {
        return map_ptr<T1>(map_, ptr_ + n, verified_block_);
    }

    template <typename S, typename T1 = T, NotVoid<T1> = 0>
    map_ptr<T1> operator-(const S n) const {
        return map_ptr<T1>(map_, ptr_ - n, verified_block_);
    }

    // Returns the value of the pointer.
    // The caller should verify the presence of the pointer data before calling this method.
    template <typename T1 = T, NotVoid<T1> = 0>
    const T1& value() const {
        LIBINCFS_MAP_PTR_DEBUG_CODE(
                CHECK(verified_) << "Did not verify presence before de-referencing safe pointer");
        return *ptr_;
    }

    // Returns a raw pointer to the value this pointer.
    // The caller should verify the presence of the pointer data before calling this method.
    template <typename T1 = T, NotVoid<T1> = 0>
    const T1* operator->() const {
        LIBINCFS_MAP_PTR_DEBUG_CODE(
                CHECK(verified_) << "Did not verify presence before de-referencing safe pointer");
        return ptr_;
    }

    // Verifies the presence of `n` elements of `T`.
    //
    // Returns true if the elements are completely present; otherwise, returns false.
    template <typename T1 = T, NotVoid<T1> = 0, bool V1 = Verified, IsUnverified<V1> = 0>
    bool verify(size_t n = 1) const {
        if (ptr_ == nullptr) {
            return false;
        }

#ifdef __ANDROID__
        if (LIKELY(map_ == nullptr)) {
            return ptr_ != nullptr;
        }

        const size_t verify_size = sizeof(T) * n;
        LIBINCFS_MAP_PTR_DEBUG_CODE(if (sizeof(T) <= verify_size) verified_ = true;);

        const auto data_start = reinterpret_cast<const uint8_t*>(ptr_);
        const auto data_end = reinterpret_cast<const uint8_t*>(ptr_) + verify_size;

        // If the data is entirely within the block beginning at the previous verified block
        // pointer, then the data can safely be used.
        if (LIKELY(data_start >= verified_block_ &&
                   data_end <= verified_block_ + INCFS_DATA_FILE_BLOCK_SIZE)) {
            return true;
        }

        if (LIKELY(map_->Verify(data_start, data_end, &verified_block_))) {
            return true;
        }

        LIBINCFS_MAP_PTR_DEBUG_CODE(verified_ = false);
        return false;
#else
        (void)n;
        return true;
#endif
    }

    // Returns a verified version of this pointer.
    // The caller should verify the presence of the pointer data before calling this method.
    template <typename T1 = T, NotVoid<T1> = 0>
    verified_map_ptr<T1> verified() const {
        return verified_map_ptr<T1>(map_, ptr_, verified_block_);
    }

private:
    map_ptr(const IncFsFileMap* map, const T* ptr)
          : ptr_(ptr), map_(map), verified_block_(nullptr) {}
    map_ptr(const IncFsFileMap* map, const T* ptr, const uint8_t* verified_block)
          : ptr_(ptr), map_(map), verified_block_(verified_block) {}

    const T* ptr_ = nullptr;
    mutable const IncFsFileMap* map_ = nullptr;
    mutable const uint8_t* verified_block_;
    LIBINCFS_MAP_PTR_DEBUG_CODE(mutable bool verified_ = Verified);
};

} // namespace incfs

} // namespace android
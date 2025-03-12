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

#ifndef TEEUI_LIBTEEUI_UTILS_H_
#define TEEUI_LIBTEEUI_UTILS_H_

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <tuple>
#include <type_traits>

#include <teeui/error.h>
#include <teeui/log.h>

namespace teeui {

using std::optional;

template <typename T, size_t elements> class Array {
    using array_type = T[elements];

  public:
    constexpr Array() : data_{} {}
    constexpr Array(const T (&data)[elements]) { std::copy(data, data + elements, data_); }
    constexpr Array(const std::initializer_list<uint8_t>& li) {
        size_t i = 0;
        for (auto& item : li) {
            data_[i] = item;
            ++i;
            if (i == elements) break;
        }
        for (; i < elements; ++i) {
            data_[i] = {};
        }
    }

    T* data() { return data_; }
    const T* data() const { return data_; }
    constexpr size_t size() const { return elements; }

    T* begin() { return data_; }
    T* end() { return data_ + elements; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + elements; }

    static constexpr Array fill(const T& v) {
        Array result;
        for (size_t i = 0; i < elements; ++i) {
            result.data_[i] = v;
        }
        return result;
    }

  private:
    array_type data_;
};

template <typename T> auto bytesCast(const T& v) -> const uint8_t (&)[sizeof(T)] {
    return *reinterpret_cast<const uint8_t(*)[sizeof(T)]>(&v);
}
template <typename T> auto bytesCast(T& v) -> uint8_t (&)[sizeof(T)] {
    return *reinterpret_cast<uint8_t(*)[sizeof(T)]>(&v);
}

class ByteBufferProxy {
    template <typename T> struct has_data {
        template <typename U> static int f(const U*, const void*) { return 0; }
        template <typename U> static int* f(const U* u, decltype(u->data())) { return nullptr; }
        static constexpr bool value = std::is_pointer<decltype(f((T*)nullptr, ""))>::value;
    };

  public:
    template <typename T>
    ByteBufferProxy(const T& buffer, decltype(buffer.data()) = nullptr)
        : data_(reinterpret_cast<const uint8_t*>(buffer.data())), size_(buffer.size()) {
        static_assert(sizeof(decltype(*buffer.data())) == 1, "elements to large");
    }

    template <size_t size>
    ByteBufferProxy(const char (&buffer)[size])
        : data_(reinterpret_cast<const uint8_t*>(buffer)), size_(size - 1) {
        static_assert(size > 0, "even an empty string must be 0-terminated");
    }

    template <size_t size>
    ByteBufferProxy(const uint8_t (&buffer)[size]) : data_(buffer), size_(size) {}

    ByteBufferProxy() : data_(nullptr), size_(0) {}

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

    const uint8_t* begin() const { return data_; }
    const uint8_t* end() const { return data_ + size_; }

  private:
    const uint8_t* data_;
    size_t size_;
};

constexpr const uint8_t kAuthTokenKeySize = 32;
constexpr const uint8_t kHmacKeySize = kAuthTokenKeySize;
using AuthTokenKey = Array<uint8_t, kAuthTokenKeySize>;
using Hmac = AuthTokenKey;

/**
 * Implementer are expected to provide an implementation with the following prototype:
 *  static optional<array<uint8_t, 32>> hmac256(const uint8_t key[32],
 *                                     std::initializer_list<ByteBufferProxy> buffers);
 */
template <typename Impl> class HMac {
  public:
    template <typename... Data>
    static optional<Hmac> hmac256(const AuthTokenKey& key, const Data&... data) {
        return Impl::hmac256(key, {data...});
    }
};

bool operator==(const ByteBufferProxy& lhs, const ByteBufferProxy& rhs);

template <typename IntType, uint32_t byteOrder> struct choose_hton;

template <typename IntType> struct choose_hton<IntType, __ORDER_LITTLE_ENDIAN__> {
    inline static IntType hton(const IntType& value) {
        IntType result = {};
        const unsigned char* inbytes = reinterpret_cast<const unsigned char*>(&value);
        unsigned char* outbytes = reinterpret_cast<unsigned char*>(&result);
        for (int i = sizeof(IntType) - 1; i >= 0; --i) {
            *(outbytes++) = inbytes[i];
        }
        return result;
    }
};

template <typename IntType> struct choose_hton<IntType, __ORDER_BIG_ENDIAN__> {
    inline static IntType hton(const IntType& value) { return value; }
};

template <typename IntType> inline IntType hton(const IntType& value) {
    return choose_hton<IntType, __BYTE_ORDER__>::hton(value);
}

template <typename IntType> inline IntType ntoh(const IntType& value) {
    // same operation as hton
    return choose_hton<IntType, __BYTE_ORDER__>::hton(value);
}

enum class Unit : uint8_t {
    PX,
    DP,
    MM,
};

template <Unit unit> struct UnitT { constexpr static const Unit value = unit; };

using px = UnitT<Unit::PX>;
using dp = UnitT<Unit::DP>;
using mm = UnitT<Unit::MM>;

template <typename Unit> inline constexpr const char* str = "N/A";

template <> inline constexpr const char* str<px> = "px";
template <> inline constexpr const char* str<dp> = "dp";
template <> inline constexpr const char* str<mm> = "mm";

using DefaultNumericType = float;

namespace bits {

inline long double abs(long double v) {
    return ::fabsl(v);
}
inline double abs(double v) {
    return ::fabs(v);
}

inline long double ceil(long double v) {
    return ::ceill(v);
}

inline double ceil(double v) {
    return ::ceil(v);
}

inline long double floor(long double v) {
    return ::floorl(v);
}

inline double floor(double v) {
    return ::floor(v);
}

inline long double sqrt(long double v) {
    return ::sqrtl(v);
}

inline double sqrt(double v) {
    return ::sqrt(v);
}

inline float round(float v) {
    return ::roundf(v);
}

inline long double round(long double v) {
    return ::roundl(v);
}

inline double round(double v) {
    return ::round(v);
}

}  // namespace bits

template <typename Unit, typename Numeric = DefaultNumericType> class Coordinate;

template <typename Numeric> struct Add {
    constexpr static Coordinate<px, Numeric> eval(const Coordinate<px, Numeric>& v1,
                                                  const Coordinate<px, Numeric>& v2) {
        return v1 + v2;
    }
};
template <typename Numeric> struct Sub {
    constexpr static Coordinate<px, Numeric> eval(const Coordinate<px, Numeric>& v1,
                                                  const Coordinate<px, Numeric>& v2) {
        return v1 - v2;
    }
};
template <typename Numeric> struct Mul {
    constexpr static Coordinate<px, Numeric> eval(const Coordinate<px, Numeric>& v1,
                                                  const Coordinate<px, Numeric>& v2) {
        return v1 * v2;
    }
};
template <typename Numeric> struct Div {
    constexpr static Coordinate<px, Numeric> eval(const Coordinate<px, Numeric>& v1,
                                                  const Coordinate<px, Numeric>& v2) {
        return v1 / v2;
    }
};

template <typename T1, typename T2, typename Numeric, template <typename> class Op> struct BinOp;

template <typename T1, typename T2, typename Numeric> using add = BinOp<T1, T2, Numeric, Add>;
template <typename T1, typename T2, typename Numeric> using sub = BinOp<T1, T2, Numeric, Sub>;
template <typename T1, typename T2, typename Numeric> using mul = BinOp<T1, T2, Numeric, Mul>;
template <typename T1, typename T2, typename Numeric> using div = BinOp<T1, T2, Numeric, Div>;

template <typename T1, typename T2, typename Numeric, template <typename> class Op> struct BinOp {
  private:
    T1 v1_;
    T2 v2_;

  public:
    constexpr BinOp(const T1& v1, const T2& v2) : v1_(v1), v2_(v2) {}
    BinOp(const BinOp&) = default;
    BinOp(BinOp&&) = default;

    template <typename Context> Coordinate<px, Numeric> eval(const Context& ctx) const {
        Coordinate<px, Numeric> v1 = ctx = v1_;
        Coordinate<px, Numeric> v2 = ctx = v2_;
        return Op<Numeric>::eval(v1, v2);
    }
    template <typename T> constexpr add<BinOp, T, Numeric> operator+(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr sub<BinOp, T, Numeric> operator-(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr mul<BinOp, T, Numeric> operator*(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr div<BinOp, T, Numeric> operator/(const T& v) const {
        return {*this, v};
    }
};

template <typename Name, typename ParamType> struct MetaParam {};

template <typename Name, typename Unit, typename Numeric>
struct MetaParam<Name, Coordinate<Unit, Numeric>> {
    template <typename T> constexpr add<MetaParam, T, Numeric> operator+(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr sub<MetaParam, T, Numeric> operator-(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr mul<MetaParam, T, Numeric> operator*(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr div<MetaParam, T, Numeric> operator/(const T& v) const {
        return {*this, v};
    }
};

template <typename Name, typename ParamType> class Param {
  private:
    ParamType param_;

  public:
    Param() : param_{} {}
    Param(const Param&) = default;
    Param(Param&&) = default;
    Param& operator=(const Param&) = default;
    Param& operator=(Param&&) = default;
    inline const ParamType& operator*() const { return param_; }
    inline ParamType& operator*() { return param_; }
    inline const ParamType* operator->() const { return &param_; }
    inline ParamType* operator->() { return &param_; }
};

template <typename Unit, typename Numeric> class Coordinate {
    Numeric value_;

  public:
    using unit_t = Unit;
    constexpr Coordinate() : value_{} {}
    constexpr Coordinate(Numeric value) : value_(value) {}
    Coordinate(const Coordinate&) = default;
    Coordinate(Coordinate&&) = default;
    template <typename N> Coordinate(const Coordinate<Unit, N>& other) {
        if constexpr (std::is_floating_point<N>::value && std::is_integral<Numeric>::value) {
            value_ = bits::round(other.count());
        } else {
            value_ = other.count();
        }
    }
    Coordinate& operator=(const Coordinate& rhs) = default;
    Coordinate& operator=(Coordinate&& rhs) = default;

    constexpr Coordinate operator-(const Coordinate& v) const { return value_ - v.value_; }
    constexpr Coordinate operator+(const Coordinate& v) const { return value_ + v.value_; }
    constexpr Coordinate& operator-=(const Coordinate& v) {
        value_ -= v.value_;
        return *this;
    }
    constexpr Coordinate& operator+=(const Coordinate& v) {
        value_ += v.value_;
        return *this;
    }
    constexpr Coordinate operator*(const Coordinate& v) const { return value_ * v.value_; }
    constexpr Coordinate& operator*=(const Coordinate& v) {
        value_ *= v.value_;
        return *this;
    }
    constexpr Coordinate operator/(const Coordinate& v) const { return value_ / v.value_; }
    constexpr Coordinate& operator/=(const Coordinate& v) {
        value_ /= v.value_;
        return *this;
    }
    constexpr Coordinate operator-() const { return -value_; }

    Coordinate abs() const { return bits::abs(value_); }
    Coordinate ceil() const { return bits::ceil(value_); }
    Coordinate floor() const { return bits::floor(value_); }
    Coordinate sqrt() const { return bits::sqrt(value_); }

    constexpr bool operator==(const Coordinate& v) const { return value_ == v.value_; }
    constexpr bool operator!=(const Coordinate& v) const { return !(*this == v); }
    constexpr bool operator<(const Coordinate& v) const { return value_ < v.value_; }
    constexpr bool operator>(const Coordinate& v) const { return v < *this; }
    constexpr bool operator<=(const Coordinate& v) const { return !(v < *this); }
    constexpr bool operator>=(const Coordinate& v) const { return !(*this < v); }

    template <typename T> constexpr add<Coordinate, T, Numeric> operator+(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr sub<Coordinate, T, Numeric> operator-(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr mul<Coordinate, T, Numeric> operator*(const T& v) const {
        return {*this, v};
    }
    template <typename T> constexpr div<Coordinate, T, Numeric> operator/(const T& v) const {
        return {*this, v};
    }

    Numeric count() const { return value_; }
};

template <typename... T> struct MetaList {};

template <typename MetaParam> struct metaParam2Param;

template <typename ParamName, typename ParamType>
struct metaParam2Param<MetaParam<ParamName, ParamType>> {
    using type = Param<ParamName, ParamType>;
};

template <typename MetaParam> struct metaParam2ParamType;

template <typename ParamName, typename ParamType>
struct metaParam2ParamType<MetaParam<ParamName, ParamType>> {
    using type = ParamType;
};

template <typename T> struct isCoordinateType { constexpr static const bool value = false; };

template <typename Unit, typename Numeric> struct isCoordinateType<Coordinate<Unit, Numeric>> {
    constexpr static const bool value = true;
};

template <typename MetaParam> struct isCoordinateParam;

template <typename ParamName, typename ParamType>
struct isCoordinateParam<MetaParam<ParamName, ParamType>> {
    constexpr static const bool value = isCoordinateType<ParamType>::value;
};

template <typename T> struct isMetaParam { constexpr static const bool value = false; };

template <typename ParamName, typename ParamType>
struct isMetaParam<MetaParam<ParamName, ParamType>> {
    constexpr static const bool value = true;
};

template <typename Param, typename Numeric = DefaultNumericType> class context;

template <typename... ParamsNames, typename... ParamTypes, typename Numeric>
class context<MetaList<MetaParam<ParamsNames, ParamTypes>...>, Numeric> {
    Numeric mm2px_;
    Numeric dp2px_;
    std::tuple<Param<ParamsNames, ParamTypes>...> params_;

    class Proxy {
        Numeric valuepx_;
        Numeric mm2px_, dp2px_;

      public:
        Proxy(Numeric valuepx, Numeric mm2px, Numeric dp2px)
            : valuepx_(valuepx), mm2px_(mm2px), dp2px_(dp2px) {}
        Proxy(const Proxy&) = default;
        Proxy(Proxy&&) = default;

        operator Coordinate<px, Numeric>() const { return valuepx_; }
        operator Coordinate<mm, Numeric>() const { return valuepx_ / mm2px_; }
        operator Coordinate<dp, Numeric>() const { return valuepx_ / dp2px_; }
    };

  public:
    explicit context(Numeric mm2px) {
        mm2px_ = mm2px;
        dp2px_ = (mm2px * 25.4) / 160.0; /* 1dp = 1/160th of an inch */
    }

    context(Numeric mm2px, Numeric dp2px) : mm2px_(mm2px), dp2px_(dp2px) {}

    context(const context&) = default;
    context(context&&) = default;
    context& operator=(const context&) = default;
    context& operator=(context&&) = default;

    template <typename MetaParam> auto& getParam() {
        return std::get<typename metaParam2Param<MetaParam>::type>(params_);
    }
    template <typename MetaParam> const auto& getParam() const {
        return std::get<typename metaParam2Param<MetaParam>::type>(params_);
    }

    template <typename MetaParam, typename = std::enable_if_t<isCoordinateParam<MetaParam>::value>>
    void setParam(const Coordinate<px, Numeric>& v) {
        *getParam<MetaParam>() = v;
    }

    template <typename MetaParam, typename Unit, typename N,
              typename = std::enable_if_t<isCoordinateParam<MetaParam>::value>>
    void setParam(const Coordinate<Unit, N>& v) {
        *getParam<MetaParam>() = *this = v;
    }

    template <typename MetaParam>
    void setParam(std::enable_if_t<!isCoordinateParam<MetaParam>::value,
                                   const typename metaParam2ParamType<MetaParam>::type>& v) {
        *getParam<MetaParam>() = v;
    }

    Proxy operator=(const Coordinate<px, Numeric>& rhs) const {
        return {rhs.count(), mm2px_, dp2px_};
    }
    Proxy operator=(const Coordinate<mm, Numeric>& rhs) const {
        return {rhs.count() * mm2px_, mm2px_, dp2px_};
    }
    Proxy operator=(const Coordinate<dp, Numeric>& rhs) const {
        return {rhs.count() * dp2px_, mm2px_, dp2px_};
    }
    template <typename T1, typename T2, template <typename> class Op>
    Proxy operator=(const BinOp<T1, T2, Numeric, Op>& rhs) const {
        return {rhs.eval(*this).count(), mm2px_, dp2px_};
    }
    template <typename ParamName, typename ParamType>
    std::enable_if_t<isCoordinateParam<MetaParam<ParamName, ParamType>>::value, Proxy>
    operator=(const MetaParam<ParamName, ParamType>&) const {
        return {getParam<MetaParam<ParamName, ParamType>>()->count(), mm2px_, dp2px_};
    }
    template <typename ParamName, typename ParamType>
    std::enable_if_t<!isCoordinateParam<MetaParam<ParamName, ParamType>>::value, const ParamType&>
    operator=(const MetaParam<ParamName, ParamType>&) const {
        return *getParam<MetaParam<ParamName, ParamType>>();
    }
    template <typename T,
              typename = std::enable_if_t<!(isMetaParam<T>::value || isCoordinateType<T>::value)>>
    inline T&& operator=(T&& v) const {
        return std::forward<T>(v);
    }
};

using dps = Coordinate<dp>;
using mms = Coordinate<mm>;
using pxs = Coordinate<px>;

constexpr dps operator""_dp(long double dp) {
    return dps(dp);
}
constexpr mms operator""_mm(long double mm) {
    return mms(mm);
}
constexpr pxs operator""_px(long double px) {
    return pxs(px);
}
constexpr dps operator""_dp(unsigned long long dp) {
    return dps(dp);
}
constexpr mms operator""_mm(unsigned long long mm) {
    return mms(mm);
}
constexpr pxs operator""_px(unsigned long long px) {
    return pxs(px);
}

template <typename Coord> class Vec2d {
    Coord x_, y_;

  public:
    constexpr Vec2d() : x_{}, y_{} {}
    constexpr Vec2d(Coord x, Coord y) : x_(x), y_(y) {}
    Vec2d(const Vec2d&) = default;
    Vec2d(Vec2d&&) = default;
    template <typename N>
    Vec2d(const Vec2d<Coordinate<typename Coord::unit_t, N>>& other)
        : x_(other.x()), y_(other.y()) {}

    Vec2d& operator=(const Vec2d& rhs) = default;
    Vec2d& operator=(Vec2d&& rhs) = default;

    Vec2d operator-(const Vec2d& rhs) const { return Vec2d(*this) -= rhs; }
    Vec2d operator+(const Vec2d& rhs) const { return Vec2d(*this) += rhs; }
    Vec2d& operator-=(const Vec2d& rhs) {
        x_ -= rhs.x_;
        y_ -= rhs.y_;
        return *this;
    }
    Vec2d& operator+=(const Vec2d& rhs) {
        x_ += rhs.x_;
        y_ += rhs.y_;
        return *this;
    }
    Coord operator*(const Vec2d& rhs) const { return (x_ * rhs.x_ + y_ * rhs.y_).count(); }
    Vec2d operator*(const Coord& f) const { return Vec2d(*this) *= f; }
    Vec2d& operator*=(const Coord& f) {
        x_ *= f;
        y_ *= f;
        return *this;
    }
    Vec2d operator/(const Coord& f) { return Vec2d(*this) /= f; }
    Vec2d& operator/=(const Coord& f) {
        x_ /= f;
        y_ /= f;
        return *this;
    }
    bool operator==(const Vec2d& rhs) const { return x_ == rhs.x() && y_ == rhs.y(); }
    Coord length() const {
        Coord factor = *this * *this;
        return bits::sqrt(factor.count());
    }
    Vec2d unit() const { return Vec2d(*this) /= length(); }
    Coord x() const { return x_; }
    Coord y() const { return y_; }
};

#ifdef TEEUI_DO_LOG_DEBUG
template <typename Unit, typename Numeric>
std::ostream& operator<<(std::ostream& out, const Coordinate<Unit, Numeric>& p) {
    out << std::setprecision(10) << p.count() << str<Unit>;
    return out;
}

template <typename Coord> std::ostream& operator<<(std::ostream& out, const Vec2d<Coord>& p) {
    out << "Vec2d(" << p.x() << ", " << p.y() << ")";
    return out;
}
#endif

using Color = uint32_t;

template <typename Coord> using Point = Vec2d<Coord>;

Color drawLinePoint(Point<pxs> a, Point<pxs> b, Point<pxs> px_origin, Color c,
                    pxs width = pxs(1.0));

Color drawCirclePoint(Point<pxs> center, pxs r, Point<pxs> px_origin, Color c);

using PxPoint = Point<pxs>;
using PxVec = Vec2d<pxs>;

/*
 * Computes the intersection of the lines given by ax + b and cy + d.
 * The result may be empty if there is no solution.
 */
optional<PxPoint> intersect(const PxVec& a, const PxPoint& b, const PxVec& c, const PxPoint& d);

namespace bits {

static constexpr const ssize_t kIntersectEmpty = -1;
/**
 * Returned by the intersect if the object is in the positive half plane of the given line.
 */
static constexpr const ssize_t kIntersectAllPositive = -2;

ssize_t intersect(const PxPoint* oBegin, const PxPoint* oEnd, const PxPoint& lineA,
                  const PxPoint& lineB, PxPoint* nBegin, PxPoint* nEnd);

pxs area(const PxPoint* begin, const PxPoint* end);

}  // namespace bits

/**
 * A ConvexObject is given by a list of 2D vertexes. Each vertex must lie on the positive half-plane
 * of the line denoted by its two predecessors. A point is considered inside of the convex object
 * if it is on the positive half-plane of all lines given by any two subsequent vertexes.
 *
 * ConvexObjects have fixed size given by the capacity template argument. The geometric object
 * that they describe may have any number of vertexes between 3 and capacity.
 */
template <size_t capacity> class ConvexObject {
    template <size_t other_cap> friend class ConvexObject;

  protected:
    PxPoint points_[capacity];
    size_t fill_;

  public:
    ConvexObject() : fill_(0) {}
    explicit constexpr ConvexObject(std::initializer_list<PxPoint> l) : fill_(0) {
        if (l.size() > capacity) return;
        for (const auto& p : l) {
            points_[fill_++] = p;
        }
    }
    ConvexObject(const ConvexObject& other) = default;
    ConvexObject(ConvexObject&& other) = default;
    ConvexObject& operator=(const ConvexObject& other) = default;
    ConvexObject& operator=(ConvexObject&& other) = default;

    constexpr size_t size() const { return fill_; }

    constexpr const PxPoint* begin() const { return &points_[0]; }
    constexpr const PxPoint* end() const { return &points_[fill_]; }

    template <size_t result_cap>
    optional<ConvexObject<result_cap>> intersect(const PxPoint& A, const PxPoint& B) const {
        static_assert(result_cap >= capacity,
                      "resulting capacity must be at least as large as the original");
        ConvexObject<result_cap> result;
        ssize_t vCount =
            bits::intersect(begin(), end(), A, B, &result.points_[0], &result.points_[result_cap]);
        if (vCount == bits::kIntersectEmpty) return {};
        // -2 is returned if the object is in the positive half plane of the line (may be tangent)
        if (vCount == bits::kIntersectAllPositive) {
            std::copy(begin(), end(), &result.points_[0]);
            vCount = fill_;
        }
        result.fill_ = vCount;
        return result;
    }

    template <size_t result_cap, size_t arg_cap>
    optional<ConvexObject<result_cap>> intersect(const ConvexObject<arg_cap>& other) const {
        return intersect<result_cap>(other.begin(), other.end());
    }

    template <size_t result_cap>
    optional<ConvexObject<result_cap>> intersect(const PxPoint* begin, const PxPoint* end) const {
        if (end - begin < 3) return {};
        auto b = begin;
        auto a = end - 1;
        auto result = intersect<result_cap>(*a, *b);
        a = b++;
        while (result && b != end) {
            result = result->template intersect<result_cap>(*a, *b);
            a = b++;
        }
        return result;
    }

    pxs area() const { return bits::area(begin(), end()); }

    void push_back(const PxPoint& p) {
        if (fill_ < capacity) {
            points_[fill_++] = p;
        }
    }
};

#ifdef TEEUI_DO_LOG_DEBUG
template <size_t capacity>
std::ostream& operator<<(std::ostream& out, const ConvexObject<capacity>& o) {
    out << "ConvexObject(";
    bool first = true;
    for (const auto& p : o) {
        if (first)
            first = false;
        else
            out << ", ";
        out << p;
    }
    out << ")";
    return out;
}
#endif

template <typename Coord> class Box {
    Point<Coord> topLeft_;
    Vec2d<Coord> extend_;

  public:
    Box() {}
    template <typename N>
    Box(const Box<Coordinate<typename Coord::unit_t, N>>& other)
        : topLeft_(other.topLeft()), extend_(other.extend()) {}
    Box(const Coord& x, const Coord& y, const Coord& w, const Coord& h)
        : topLeft_(x, y), extend_(w, h) {}
    Box(const Point<Coord>& topLeft, const Vec2d<Coord>& extend)
        : topLeft_(topLeft), extend_(extend) {}
    bool contains(Point<Coord> p) const {
        p -= topLeft_;
        return p.y().count() >= 0 && p.y().count() <= extend_.y().count() && p.x().count() >= 0 &&
               p.x().count() <= extend_.x().count();
    }
    bool contains(const Box& other) const {
        auto br = bottomRight();
        auto obr = other.bottomRight();
        return topLeft_.x() <= other.topLeft_.x() && br.x() >= obr.x() &&
               topLeft_.y() <= other.topLeft_.y() && br.y() >= obr.y();
    }
    bool overlaps(const Box& other) const {
        auto br = bottomRight();
        auto obr = other.bottomRight();
        return topLeft_.x() < obr.x() && other.topLeft_.x() < br.x() && topLeft_.y() < obr.y() &&
               other.topLeft_.y() < obr.y();
    }

    /**
     * fitsInside only compares the extend of the boxes. It returns true if this box would fit
     * inside the other box regardless of their absolute positions.
     */
    bool fitsInside(const Box& other) const { return w() <= other.w() && h() <= other.w(); }
    Point<Coord> bottomRight() const { return topLeft_ + extend_; }
    Point<Coord> topLeft() const { return topLeft_; }
    Vec2d<Coord> extend() const { return extend_; }
    Coord x() const { return topLeft_.x(); }
    Coord y() const { return topLeft_.y(); }
    Coord w() const { return extend_.x(); }
    Coord h() const { return extend_.y(); }

    Box merge(const Box& other) const {
        Coord x = std::min(topLeft_.x(), other.topLeft_.x());
        Coord y = std::min(topLeft_.y(), other.topLeft_.y());
        auto br = bottomRight();
        auto obr = other.bottomRight();
        Coord w = std::max(br.x(), obr.x()) - x;
        Coord h = std::max(br.y(), obr.y()) - y;
        return {x, y, w, h};
    }

    /**
     * Returns a box that contains the this box and the given point.
     */
    Box merge(const Point<Coord>& p) const {
        auto br = bottomRight();
        TEEUI_LOG << "A tl: " << topLeft_ << " br: " << br << " new: " << p << ENDL;
        Coord x = std::min(topLeft_.x(), p.x());
        Coord y = std::min(topLeft_.y(), p.y());
        Coord w = std::max(br.x(), p.x()) - x;
        Coord h = std::max(br.y(), p.y()) - y;
        TEEUI_LOG << "B x: " << x << " y: " << y << " w: " << w << " h: " << h << ENDL;
        return {x, y, w, h};
    }

    /**
     * Returns a box that contains this box and all of the given points.
     */
    Box merge(const Point<Coord>* begin, const Point<Coord>* end) const {
        auto tl = topLeft();
        auto br = bottomRight();
        while (begin != end) {
            TEEUI_LOG << "A tl: " << tl << " br: " << br << " new: " << *begin << ENDL;
            tl = {std::min(tl.x(), begin->x()), std::min(tl.y(), begin->y())};
            br = {std::max(br.x(), begin->x()), std::max(br.y(), begin->y())};
            TEEUI_LOG << "B tl: " << tl << " br: " << br << " new: " << *begin << ENDL;
            ++begin;
        }
        return {tl, br - tl};
    }

    /**
     * Creates a box that contains all of the given points.
     */
    static Box boundingBox(const Point<Coord>* begin, const Point<Coord>* end) {
        if (begin == end) return {};
        Box result(*begin, {0, 0});
        result.merge(begin + 1, end);
        return result;
    }

    /*
     * Translates the Box by the given offset. And returns a reference to itself
     */
    Box& translateSelf(const Point<Coord>& offset) {
        topLeft_ += offset;
        return *this;
    }
    /*
     * Returns copy of this box translated by offset.
     */
    Box translate(const Point<Coord>& offset) const& {
        Box result = *this;
        result.topLeft_ += offset;
        return result;
    }
    /*
     * When called on a temporary just reuse this box.
     */
    Box translate(const Point<Coord>& offset) && {
        topLeft_ += offset;
        return *this;
    }
};

#ifdef TEEUI_DO_LOG_DEBUG
template <typename Coord> std::ostream& operator<<(std::ostream& out, const Box<Coord>& p) {
    out << "Box(x: " << p.x().count() << " y: " << p.y().count() << " w: " << p.w().count()
        << " h: " << p.h().count() << ")";
    return out;
}
#endif

enum class EventType : uint8_t {
    KeyDown,
    KeyUp,
    KeyMoved,
};

struct Event {
    uint32_t x_;
    uint32_t y_;
    EventType event_;
};

template <typename Fn> struct Callback;

template <typename Ret, typename... Args> struct Callback<Ret(Args...)> {
    Ret (*callback_)(Args... args, void* priv_data);
    void* priv_data_;
    Ret operator()(Args... args) const { return callback_(args..., priv_data_); }
};

template <typename Ret, typename... Args>
Callback<Ret(Args...)> makeCallback(Ret (*fn)(Args..., void*), void* priv_data) {
    return {fn, priv_data};
}

template <typename Fn, typename Ret, typename... Args> struct CallbackHelper {
    Fn fn_;
    operator Callback<Ret(Args...)>() {
        return makeCallback<Ret, Args...>(
            [](Args... args, void* priv_data) -> Ret {
                return reinterpret_cast<CallbackHelper*>(priv_data)->fn_(args...);
            },
            this);
    }
};

using CallbackEvent = Callback<Error(Event)>;
using PixelDrawer = Callback<Error(uint32_t, uint32_t, Color)>;

template <typename Fn>
using PixelDrawerHelper = CallbackHelper<Fn, Error, uint32_t, uint32_t, Color>;

template <typename Fn> PixelDrawerHelper<Fn> makePixelDrawer(Fn fn) {
    return PixelDrawerHelper<Fn>{fn};
}

template <typename Derived> struct LayoutElement {
    Box<pxs> bounds_;
    LayoutElement() = default;
    template <typename Context>
    LayoutElement(const Context& context)
        : bounds_{context = Derived::pos_x, context = Derived::pos_y, context = Derived::dim_w,
                  context = Derived::dim_h} {}

    Error draw(const PixelDrawer&) { return Error::OK; }
    Error hit(const Event&) { return Error::OK; }
};

template <typename... Elements, typename Context>
std::tuple<Elements...> instantiateLayout(MetaList<Elements...>, const Context& context) {
    std::tuple<Elements...> result{Elements(context)...};
    return result;
}

template <typename T> struct MetaList2Layout;

template <typename... Elements> struct MetaList2Layout<MetaList<Elements...>> {
    using type = std::tuple<Elements...>;
};

template <typename T> using layout_t = typename MetaList2Layout<T>::type;

template <typename... Coords>
constexpr inline std::tuple<Vec2d<Coords>...> makeConvexObject(const Vec2d<Coords>&... points) {
    return {points...};
}

template <size_t capacity, typename Tuple, typename Context, size_t... I>
constexpr inline ConvexObject<capacity>
initConvexObject(const Context& context, const Tuple& outline, std::index_sequence<I...>) {
    return ConvexObject<capacity>(
        {PxVec(context = std::get<I>(outline).x(), context = std::get<I>(outline).y())...});
}

template <size_t capacity, typename... Points, typename Context>
constexpr inline ConvexObject<capacity> initConvexObject(const Context& context,
                                                         const std::tuple<Points...>& outline) {
    return initConvexObject<capacity>(context, outline, std::index_sequence_for<Points...>{});
}

template <size_t capacity, typename Tuple, typename Context, size_t... I, size_t size>
constexpr inline void initConvexObjectArray(const Context& context,
                                            ConvexObject<capacity> (&out)[size], const Tuple& t,
                                            std::index_sequence<I...>) {
    static_assert(sizeof...(I) <= size,
                  "Array to initialize must be big enough to hold all tuple elements");
    [](auto...) {}((out[I] = initConvexObject<capacity>(context, std::get<I>(t)))...);
}

template <size_t capacity, typename... COs, typename Context, size_t size>
constexpr inline void initConvexObjectArray(const Context& context,
                                            ConvexObject<capacity> (&out)[size],
                                            const std::tuple<COs...>& t) {
    initConvexObjectArray(context, out, t, std::index_sequence_for<COs...>());
}

template <typename Iterator> class Range {
    Iterator begin_;
    Iterator end_;

  public:
    Range(Iterator begin, Iterator end) : begin_(begin), end_(end) {}
    const Iterator begin() const { return begin_; }
    const Iterator end() const { return end_; }
};

template <typename Iterator> Range<Iterator> makeRange(Iterator begin, Iterator end) {
    return {begin, end};
}

}  // namespace teeui

#define Position(x, y)                                                                             \
    static const constexpr auto pos_x = x;                                                         \
    static const constexpr auto pos_y = y

#define Dimension(w, h)                                                                            \
    static const constexpr auto dim_w = w;                                                         \
    static const constexpr auto dim_h = h

#define BEGIN_ELEMENT(name, type, ...)                                                             \
    struct name : public type<name, ##__VA_ARGS__> {                                               \
        name() = default;                                                                          \
        template <typename Context>                                                                \
        name(const Context& context) : type<name, ##__VA_ARGS__>(context) {}

#define END_ELEMENT() }

#define DECLARE_TYPED_PARAMETER(name, type)                                                        \
    struct Param_##name {};                                                                        \
    using name = ::teeui::MetaParam<Param_##name, type>

#define DECLARE_PARAMETER(name) DECLARE_TYPED_PARAMETER(name, ::teeui::pxs)

#define CONSTANT(name, value) static constexpr const auto name = value

#define BOTTOM_EDGE_OF(name) (name::pos_y + name::dim_h)

#define CONVEX_OBJECT(...) makeConvexObject(__VA_ARGS__)

#define CONVEX_OBJECTS(...) std::make_tuple(__VA_ARGS__)

/**
 * Creates a new Layout with the name "name" followed by a list of Layout elements as defined with
 * BEGIN_ELEMENT(name).
 */
#define NEW_LAYOUT(name, ...) using name = ::teeui::MetaList<__VA_ARGS__>

#define NEW_PARAMETER_SET(name, ...) using name = ::teeui::MetaList<__VA_ARGS__>

#define LABELS(name, ...) using ::teeui::MetaList<__VA_ARGS__>

#define TEXT_ID(textId) static_cast<uint32_t>(textId)
#endif  // TEEUI_LIBTEEUI_UTILS_H_

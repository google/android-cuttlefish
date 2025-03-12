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

#include <teeui/utils.h>

namespace teeui {

bool operator==(const ByteBufferProxy& lhs, const ByteBufferProxy& rhs) {
    if (lhs.size() == rhs.size()) {
        auto lhsi = lhs.begin();
        auto rhsi = rhs.begin();
        while (lhsi != lhs.end()) {
            if (*lhsi++ != *rhsi++) return false;
        }
    }
    return true;
}

constexpr const DefaultNumericType kEpsilon = 0.000001;
constexpr const DefaultNumericType kHalfSqrt2 = 0.70710678118;

Color pixelLineIntersect(Point<pxs> line, pxs dist, Color c) {
    TEEUI_LOG << "Line: " << line << " Dist: " << dist;
    bool more_than_half = dist < 0.0;
    TEEUI_LOG << " " << more_than_half;

    Color intensity = 0;
    if (dist.abs() < kEpsilon) {
        intensity = 0x80;
        TEEUI_LOG << " half covered";
    } else if (dist.abs() >= kHalfSqrt2) {
        intensity = more_than_half ? 0xff : 0;
        TEEUI_LOG << (more_than_half ? " fully covered" : " not covered");
    } else {
        auto dist_vec = line * dist;
        TEEUI_LOG << " vec " << dist_vec;
        dist_vec = Point<pxs>(dist_vec.x().abs(), dist_vec.y().abs());
        TEEUI_LOG << " vec " << dist_vec;
        if (dist_vec.x() < dist_vec.y()) {
            dist_vec = Point<pxs>(dist_vec.y(), dist_vec.x());
        }
        auto a0 = dist_vec.x();
        auto a1 = -dist_vec.y();
        pxs area(.0);
        if (a1 > -kEpsilon) {
            TEEUI_LOG << " X";
            area = a0;
        } else {
            Point<pxs> Q(a1 * (a1 + pxs(.5)) / a0 + a0, pxs(-.5));
            if (Q.x() >= pxs(.5)) {
                // line does not intersect our pixel.
                intensity = more_than_half ? 0xff : 0;
                TEEUI_LOG << (more_than_half ? " fully covered (2)" : " not covered(2)");
            } else {
                TEEUI_LOG << " partially covered";
                Point<pxs> P(pxs(.5), a1 - a0 * (pxs(.5) - a0) / a1);
                TEEUI_LOG << " P: " << P << " Q: " << Q;
                Point<pxs> R = P - Q;
                TEEUI_LOG << " R: " << R;
                area = R.x() * R.y() * pxs(.5);
                if (R.y() > 1.0) {
                    auto r = R.y() - pxs(1.0);
                    area -= r * R.x() * ((r) / R.y()) * pxs(.5);
                }
            }
        }
        if (more_than_half) {
            area = pxs(1.0) - area;
        }
        TEEUI_LOG << " area: " << area;
        intensity = area.count() * 0xff;
    }
    TEEUI_LOG << ENDL;
    return intensity << 24 | (c & 0xffffff);
}

Color drawLinePoint(Point<pxs> a, Point<pxs> b, Point<pxs> px_origin, Color c, pxs width) {
    auto line = a - b;
    auto len = line.length();
    auto l = line / len;
    auto seg = l * (px_origin - b);
    auto dist = 0_px;
    if (seg < 0_px) {
        //        line = px_origin - b;
        //        dist = line.length();
        //        line /= dist;
        //        dist -= width;
        return 0;
    } else if (seg > len) {
        //        line = px_origin - a;
        //        dist = line.length();
        //        line /= dist;
        //        dist -= width;
        return 0;
    } else {
        line = Point<pxs>(-line.y(), line.x()) / len;
        dist = (line * (px_origin - a)).abs() - width + .5_px;
    }

    return pixelLineIntersect(line, dist, c);
}

Color drawCirclePoint(Point<pxs> center, pxs r, Point<pxs> px_origin, Color c) {
    auto line = px_origin - center;
    auto dist = line.length() - r;

    return pixelLineIntersect(line.unit(), dist, c);
}

/*
 * Computes the intersection of the lines given by ax + b and cy + d.
 * The result may be empty if there is no solution.
 */
optional<PxPoint> intersect(const PxVec& a, const PxPoint& b, const PxVec& c, const PxPoint& d) {
    pxs y = 0.0;
    PxVec g = b - d;
    if (a.x().abs() < kEpsilon) {
        if (c.x().abs() < kEpsilon || a.y() < kEpsilon) {
            return {};
        } else {
            y = g.x() / c.x();
        }
    } else {
        pxs f = a.y() / a.x();
        pxs h = f * c.x() - c.y();
        if (h.abs() < kEpsilon) {
            return {};
        } else {
            y = (f * g.x() - g.y()) / h;
        }
    }
    return c * y + d;
}

namespace bits {

template <typename VectorType> inline VectorType rotate90(const VectorType& in) {
    return {-in.y(), in.x()};
}

ssize_t intersect(const PxPoint* oBegin, const PxPoint* oEnd, const PxPoint& lineA,
                  const PxPoint& lineB, PxPoint* nBegin, PxPoint* nEnd) {

    auto line = lineB - lineA;
    if (oBegin == oEnd) return kIntersectEmpty;
    auto b = oBegin;
    auto a = b;
    ++b;
    auto nCur = nBegin;
    unsigned int intersections_found = 0;
    // inside indicates if we are inside the new convex object.
    // If we happen to transition from inside to inside, we know that we where wrong and
    // reset the output object. But if we were on the inside we have the full new object once
    // we have traveled around the old object once.
    bool inside = true;

    auto processSegment = [&](const PxVec& a, const PxVec& b) -> bool {
        auto segment = b - a;
        if (auto p = intersect(line, lineA, segment, a)) {
            auto seg_len = segment.length();
            auto aDist = (segment * (*p - a)) / seg_len;
            if (aDist >= 0.0 && aDist < segment.length()) {
                ++intersections_found;
                // The line vector points toward the negative half plain of segment.
                // This means we are entering the resulting convex object.
                bool enter = rotate90(segment) * line < 0;
                if (enter && inside) {
                    // if we are entering the object, but we thought we are already inside, we
                    // forget all previous points, because we were wrong.
                    if (intersections_found < 2) {
                        // Only do after we found the first intersection. Other cases are likely
                        // duplications due to rounding errors.
                        nCur = nBegin;
                    }
                }
                TEEUI_LOG << *p << " inside: " << inside << " enter: " << enter << ENDL;
                inside = enter;
                // an intersection of the new line and a segment is always part of the resulting
                // object.
                if (nCur == nEnd) {
                    TEEUI_LOG << "error out of space 1" << ENDL;
                    return false;
                }
                if (aDist > 0.0 || enter) {
                    TEEUI_LOG << "add P: " << *p << ENDL;
                    *nCur++ = *p;
                }
            }
        }
        if (nCur == nEnd) {
            TEEUI_LOG << "error out of space 2" << ENDL;
            return false;
        }
        if (inside) {
            TEEUI_LOG << "add B: " << b << ENDL;
            *nCur++ = b;
        }
        return true;
    };

    while (b != oEnd) {
        if (!processSegment(*a, *b)) return kIntersectEmpty;
        a = b++;
    }
    if (!processSegment(*a, *oBegin)) return kIntersectEmpty;

    TEEUI_LOG << "intersections found: " << intersections_found << ENDL;
    // handle tangents and disjunct case
    if (intersections_found < 2) {
        // find a point that is not on the line
        // if there is at most one intersection, all points of the object are on the same half
        // plane or on the line.
        a = oBegin;
        pxs d;
        do {
            d = rotate90(line) * (*a - lineA);
            if (++a == oEnd) {
                TEEUI_LOG << "error no point with distance > 0" << ENDL;
                return kIntersectEmpty;
            }
        } while (d == 0.0);

        if (d > 0) {
            // positive half plane
            return kIntersectAllPositive;
        } else {
            // negative half plane
            TEEUI_LOG << "egative half plane" << ENDL;
            return kIntersectEmpty;
        }
    }

    return nCur - nBegin;
}

pxs area(const PxPoint* begin, const PxPoint* end) {
    if (end - begin < 3) return 0.0;
    auto o = *begin;
    auto a = begin;
    ++a;
    auto b = a;
    ++b;
    pxs result = 0;
    while (b != end) {
        auto x = *a - o;
        auto y = *b - o;
        result += x.x() * y.y() - x.y() * y.x();
        a = b;
        ++b;
    }
    result /= 2;
    return result;
}

}  // namespace bits

}  // namespace teeui

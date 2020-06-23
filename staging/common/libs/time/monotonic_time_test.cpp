/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include "common/libs/time/monotonic_time.h"

#include <gtest/gtest.h>
#include <algorithm>

using cuttlefish::time::TimeDifference;

class MonotonicTimeTest : public ::testing::Test {
 public:
  MonotonicTimeTest() {}
};

TEST_F(MonotonicTimeTest, TimeDifferenceAdd1) {
  TimeDifference td1(1, 10, 1);
  TimeDifference td2(0, 100, 1);
  EXPECT_EQ((td1+td2).count(), (1)*1000000000 + 110);
}

TEST_F(MonotonicTimeTest, TimeDifferenceAdd2) {
  TimeDifference td1(10, 1000, 1);
  TimeDifference td2(100, 10000, 1);
  EXPECT_EQ((td1+td2).count(), (110L)*1000000000L + 11000L);
}

TEST_F(MonotonicTimeTest, TimeDifferenceAdd3) {
  int64_t scale = 1000;
  TimeDifference td1(10, 1000, scale);
  TimeDifference td2(100, 10000, scale);
  EXPECT_EQ((td1+td2).count(), ((110L)*1000000000L + 11000L)/scale);
}

TEST_F(MonotonicTimeTest, TimeDifferenceAdd4) {
  int64_t scale = 1;
  TimeDifference td1(-10, 1000, scale);
  TimeDifference td2(100, 10000, scale);
  EXPECT_EQ((td1+td2).count(), ((90L)*1000000000L + 11000L)/scale);
}

TEST_F(MonotonicTimeTest, TimeDifferenceAdd5) {
  int64_t scale1 = 1, scale2 = 1000;
  TimeDifference td1(-10, 1000, scale1);
  TimeDifference td2(100, 10000, scale2);
  EXPECT_EQ((td1+td2).count(), ((90L)*1000000000L + 11000L)/std::min(scale1, scale2));
}

TEST_F(MonotonicTimeTest, TimeDifferenceAdd6) {
  int64_t scale1 = 1000, scale2 = 1000;
  TimeDifference td1(0, 995, scale1);
  TimeDifference td2(0, 10, scale2);
  EXPECT_EQ((td1+td2).count(), (1005L)/std::min(scale1, scale2));
}

TEST_F(MonotonicTimeTest, TimeDifferenceSub1) {
  int64_t scale = 1;
  TimeDifference td1(10, 1000, scale);
  TimeDifference td2(100, 10000, scale);
  EXPECT_EQ((td2-td1).count(), ((90L)*1000000000L + 9000L)/scale);
}

TEST_F(MonotonicTimeTest, TimeDifferenceSub2) {
  int64_t scale = 1;
  TimeDifference td1(10, 1000, scale);
  TimeDifference td2(100, 10000, scale);
  EXPECT_EQ((td1-td2).count(), ((-91L)*1000000000L + 1000000000L - 9000L)/scale);
}

TEST_F(MonotonicTimeTest, TimeDifferenceSub3) {
  int64_t scale1 = 1, scale2 = 1000;
  TimeDifference td1(-10, 1000, scale1);
  TimeDifference td2(100, 10000, scale2);
  EXPECT_EQ((td1-td2).count(), ((-111L)*1000000000L + 1000000000L - 9000L)/std::min(scale1, scale2));
}

TEST_F(MonotonicTimeTest, TimeDifferenceSub4) {
  int64_t scale1 = 1000, scale2 = 1000;
  TimeDifference td1(0, 995, scale1);
  TimeDifference td2(0, 10, scale2);
  EXPECT_EQ((td1-td2).count(), (985L)/std::min(scale1, scale2));
}

TEST_F(MonotonicTimeTest, TimeDifferenceComp1) {
  int64_t scale = 1;
  TimeDifference td1(10, 10000, scale);
  TimeDifference td2(100, 10, scale);
  EXPECT_TRUE((td1 < td2));
  EXPECT_FALSE(td2 < td1);
}

TEST_F(MonotonicTimeTest, TimeDifferenceComp2) {
  int64_t scale = 1;
  TimeDifference td1(100, 10000, scale);
  TimeDifference td2(100, 10, scale);
  EXPECT_TRUE((td2 < td1));
  EXPECT_FALSE(td1 < td2);
}

/*
 * Copyright (C) 2015-2022 The Android Open Source Project
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

#include <android-base/file.h>
#include <gtest/gtest.h>
#include <fstream>
#include "host/libs/location/GpxParser.h"

namespace cuttlefish {

namespace {
bool ParseGpxData(GpsFixArray* locations, char* text, std::string* error) {
  bool result;
  TemporaryDir myDir;
  std::string path = std::string(myDir.path) + "/" + "test.gpx";

  std::ofstream myfile;
  myfile.open(path.c_str());
  myfile << text;
  myfile.close();
  result = GpxParser::parseFile(path.c_str(), locations, error);
  return result;
}
}  // namespace

TEST(GpxParser, ParseFileNotFound) {
  GpsFixArray locations;
  std::string error;
  bool isOk = GpxParser::parseFile("i_dont_exist.gpx", &locations, &error);
  EXPECT_FALSE(isOk);
}

TEST(GpxParser, ParseFileEmpty) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);

  EXPECT_TRUE(isOk);
  EXPECT_EQ(0U, locations.size());
}

TEST(GpxParser, ParseFileEmptyRteTrk) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<rte>"
      "</rte>"
      "<trk>"
      "<trkseg>"
      "</trkseg>"
      "</trk>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(0U, locations.size());
}

TEST(GpxParser, ParseFileValid) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lon=\"0\" lat=\"0\">"
      "<name>Wpt 1</name>"
      "</wpt>"
      "<wpt lon=\"0\" lat=\"0\">"
      "<name>Wpt 2</name>"
      "</wpt>"
      "<rte>"
      "<rtept lon=\"0\" lat=\"0\">"
      "<name>Rtept 1</name>"
      "</rtept>"
      "<rtept lon=\"0\" lat=\"0\">"
      "<name>Rtept 2</name>"
      "</rtept>"
      "</rte>"
      "<trk>"
      "<trkseg>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 1-1</name>"
      "</trkpt>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 1-2</name>"
      "</trkpt>"
      "</trkseg>"
      "<trkseg>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 2-1</name>"
      "</trkpt>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 2-2</name>"
      "</trkpt>"
      "</trkseg>"
      "</trk>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_TRUE(isOk);
  ASSERT_EQ(8U, locations.size());

  EXPECT_EQ("Wpt 1", locations[0].name);
  EXPECT_EQ("Wpt 2", locations[1].name);
  EXPECT_EQ("Rtept 1", locations[2].name);
  EXPECT_EQ("Rtept 2", locations[3].name);
  EXPECT_EQ("Trkpt 1-1", locations[4].name);
  EXPECT_EQ("Trkpt 1-2", locations[5].name);
  EXPECT_EQ("Trkpt 2-1", locations[6].name);
  EXPECT_EQ("Trkpt 2-2", locations[7].name);
}

TEST(GpxParser, ParseFileNullAttribute) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lon=\"0\" lat=\"0\">"
      "<name/>"
      "</wpt>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);

  // This test only checks if GpxParser doesn't crash on null attributes
  // So if we're here it's already Ok - these tests aren't that relevant.
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  EXPECT_STREQ("", locations[0].name.c_str());
  EXPECT_TRUE(error.empty());
}

TEST(GpxParser, ParseLocationMissingLatitude) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lon=\"9.81\">"
      "<ele>6.02</ele>"
      "<name>Name</name>"
      "<desc>Desc</desc>"
      "</wpt>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_FALSE(isOk);
}

TEST(GpxParser, ParseLocationMissingLongitude) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lat=\"3.1415\">"
      "<ele>6.02</ele>"
      "<name>Name</name>"
      "<desc>Desc</desc>"
      "</wpt>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_FALSE(isOk);
}

TEST(GpxParser, ParseValidLocation) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lon=\"9.81\" lat=\"3.1415\">"
      "<ele>6.02</ele>"
      "<name>Name</name>"
      "<desc>Desc</desc>"
      "</wpt>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  const GpsFix& wpt = locations[0];

  EXPECT_EQ("Desc", wpt.description);
  EXPECT_FLOAT_EQ(6.02, wpt.elevation);
  EXPECT_FLOAT_EQ(3.1415, wpt.latitude);
  EXPECT_FLOAT_EQ(9.81, wpt.longitude);
  EXPECT_EQ("Name", wpt.name);
}

// Flaky test; uses locale.
TEST(GpxParser, DISABLED_ParseValidLocationCommaLocale) {
  // auto scopedCommaLocale = setScopedCommaLocale();
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lon=\"9.81\" lat=\"3.1415\">"
      "<ele>6.02</ele>"
      "<name>Name</name>"
      "<desc>Desc</desc>"
      "</wpt>"
      "</gpx>";

  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  const GpsFix& wpt = locations[0];

  EXPECT_EQ("Desc", wpt.description);
  EXPECT_FLOAT_EQ(6.02, wpt.elevation);
  EXPECT_FLOAT_EQ(3.1415, wpt.latitude);
  EXPECT_FLOAT_EQ(9.81, wpt.longitude);
  EXPECT_EQ("Name", wpt.name);
}

TEST(GpxParser, ParseValidDocument) {
  char text[] =
      "<?xml version=\"1.0\"?>"
      "<gpx>"
      "<wpt lon=\"0\" lat=\"0\">"
      "<name>Wpt 1</name>"
      "</wpt>"
      "<wpt lon=\"0\" lat=\"0\">"
      "<name>Wpt 2</name>"
      "</wpt>"
      "<rte>"
      "<rtept lon=\"0\" lat=\"0\">"
      "<name>Rtept 1</name>"
      "</rtept>"
      "<rtept lon=\"0\" lat=\"0\">"
      "<name>Rtept 2</name>"
      "</rtept>"
      "</rte>"
      "<trk>"
      "<trkseg>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 1-1</name>"
      "</trkpt>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 1-2</name>"
      "</trkpt>"
      "</trkseg>"
      "<trkseg>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 2-1</name>"
      "</trkpt>"
      "<trkpt lon=\"0\" lat=\"0\">"
      "<name>Trkpt 2-2</name>"
      "</trkpt>"
      "</trkseg>"
      "</trk>"
      "</gpx>";
  GpsFixArray locations;
  std::string error;
  bool isOk = ParseGpxData(&locations, text, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(8U, locations.size());

  EXPECT_EQ("Wpt 1", locations[0].name);
  EXPECT_EQ("Wpt 2", locations[1].name);
  EXPECT_EQ("Rtept 1", locations[2].name);
  EXPECT_EQ("Rtept 2", locations[3].name);
  EXPECT_EQ("Trkpt 1-1", locations[4].name);
  EXPECT_EQ("Trkpt 1-2", locations[5].name);
  EXPECT_EQ("Trkpt 2-1", locations[6].name);
  EXPECT_EQ("Trkpt 2-2", locations[7].name);
}

}  // namespace cuttlefish
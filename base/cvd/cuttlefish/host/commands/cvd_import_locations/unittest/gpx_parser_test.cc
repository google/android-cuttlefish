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
#include "host/libs/location/GpsFix.h"
#include "host/libs/location/GpxParser.h"
#include "host/libs/location/StringParse.h"

namespace cuttlefish {

namespace {
bool ParseGpxFile(GpsFixArray* locations, char* text, std::string* error) {
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

bool ParseGpxString(GpsFixArray* locations, char* text, std::string* error) {
  bool result;
  result = GpxParser::parseString(text, strlen(text), locations, error);
  return result;
}

}  // namespace

TEST(GpxParser, ParseFileNotFound) {
  GpsFixArray locations;
  std::string error;
  bool isOk = GpxParser::parseFile("i_dont_exist.gpx", &locations, &error);
  EXPECT_FALSE(isOk);
}

char kEmptyText[] =
    "<?xml version=\"1.0\"?>"
    "<gpx>"
    "</gpx>";
TEST(GpxParser, ParseEmptyFile) {
  std::string error;
  bool isOk;
  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kEmptyText, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(0U, locations.size());
}

TEST(GpxParser, ParseEmptyString) {
  std::string error;
  bool isOk;
  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kEmptyText, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(0U, locations.size());
}

char kEmptyRteTrkText[] =
    "<?xml version=\"1.0\"?>"
    "<gpx>"
    "<rte>"
    "</rte>"
    "<trk>"
    "<trkseg>"
    "</trkseg>"
    "</trk>"
    "</gpx>";
TEST(GpxParser, ParseEmptyRteTrkFile) {
  std::string error;
  bool isOk;
  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kEmptyRteTrkText, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(0U, locations.size());
}

TEST(GpxParser, ParseEmptyRteTrkString) {
  std::string error;
  bool isOk;
  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kEmptyRteTrkText, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(0U, locations.size());
}

char kValidText[] =
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
TEST(GpxParser, ParseValidFile) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kValidText, &error);
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

TEST(GpxParser, ParseValidString) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kValidText, &error);
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

char kNullAttributeText[] =
    "<?xml version=\"1.0\"?>"
    "<gpx>"
    "<wpt lon=\"0\" lat=\"0\">"
    "<name/>"
    "</wpt>"
    "</gpx>";
TEST(GpxParser, ParseFileNullAttributeFile) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kNullAttributeText, &error);
  // This test only checks if GpxParser doesn't crash on null attributes
  // So if we're here it's already Ok - these tests aren't that relevant.
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  EXPECT_STREQ("", locations[0].name.c_str());
  EXPECT_TRUE(error.empty());
}

TEST(GpxParser, ParseFileNullAttributeString) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kNullAttributeText, &error);
  // This test only checks if GpxParser doesn't crash on null attributes
  // So if we're here it's already Ok - these tests aren't that relevant.
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  EXPECT_STREQ("", locations[0].name.c_str());
  EXPECT_TRUE(error.empty());
}

char kLocationMissingLongitudeLatitudeText[] =
    "<?xml version=\"1.0\"?>"
    "<gpx>"
    "<wpt lon=\"9.81\">"
    "<ele>6.02</ele>"
    "<name>Name</name>"
    "<desc>Desc</desc>"
    "</wpt>"
    "</gpx>";
TEST(GpxParser, ParseLocationMissingLatitudeFile) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk =
      ParseGpxFile(&locations, kLocationMissingLongitudeLatitudeText, &error);
  EXPECT_FALSE(isOk);
}

TEST(GpxParser, ParseLocationMissingLatitudeString) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk =
      ParseGpxString(&locations, kLocationMissingLongitudeLatitudeText, &error);
  EXPECT_FALSE(isOk);
}

char kLocationMissingLongitudeText[] =
    "<?xml version=\"1.0\"?>"
    "<gpx>"
    "<wpt lat=\"3.1415\">"
    "<ele>6.02</ele>"
    "<name>Name</name>"
    "<desc>Desc</desc>"
    "</wpt>"
    "</gpx>";
TEST(GpxParser, ParseLocationMissingLongitudeFile) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kLocationMissingLongitudeText, &error);
  EXPECT_FALSE(isOk);
}

TEST(GpxParser, ParseLocationMissingLongitudeString) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kLocationMissingLongitudeText, &error);
  EXPECT_FALSE(isOk);
}

char kValidLocationText[] =
    "<?xml version=\"1.0\"?>"
    "<gpx>"
    "<wpt lon=\"9.81\" lat=\"3.1415\">"
    "<ele>6.02</ele>"
    "<name>Name</name>"
    "<desc>Desc</desc>"
    "</wpt>"
    "</gpx>";
TEST(GpxParser, ParseValidLocationFile) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kValidLocationText, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  const GpsFix& wpt = locations[0];
  EXPECT_EQ("Desc", wpt.description);
  EXPECT_FLOAT_EQ(6.02, wpt.elevation);
  EXPECT_FLOAT_EQ(3.1415, wpt.latitude);
  EXPECT_FLOAT_EQ(9.81, wpt.longitude);
  EXPECT_EQ("Name", wpt.name);
}

TEST(GpxParser, ParseValidLocationString) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kValidLocationText, &error);
  EXPECT_TRUE(isOk);
  EXPECT_EQ(1U, locations.size());
  const GpsFix& wpt = locations[0];
  EXPECT_EQ("Desc", wpt.description);
  EXPECT_FLOAT_EQ(6.02, wpt.elevation);
  EXPECT_FLOAT_EQ(3.1415, wpt.latitude);
  EXPECT_FLOAT_EQ(9.81, wpt.longitude);
  EXPECT_EQ("Name", wpt.name);
}

char kValidDocumentText[] =
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
TEST(GpxParser, ParseValidDocumentFile) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxFile(&locations, kValidDocumentText, &error);
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

TEST(GpxParser, ParseValidDocumentString) {
  std::string error;
  bool isOk;

  GpsFixArray locations;
  isOk = ParseGpxString(&locations, kValidDocumentText, &error);
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
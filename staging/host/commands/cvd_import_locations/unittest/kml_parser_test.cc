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
#include "host/libs/location/KmlParser.h"
#include "host/libs/location/StringParse.h"

namespace cuttlefish {
namespace {
bool ParseKmlFile(GpsFixArray* locations, char* text, std::string* error) {
  bool result;
  TemporaryDir myDir;
  std::string path = std::string(myDir.path) + "/" + "test.kml";

  std::ofstream myfile;
  myfile.open(path.c_str());
  myfile << text;
  myfile.close();
  result = KmlParser::parseFile(path.c_str(), locations, error);
  return result;
}

bool ParseKmlString(GpsFixArray* locations, char* text, std::string* error) {
  bool result;
  result = KmlParser::parseString(text, strlen(text), locations, error);
  return result;
}
}  // namespace

TEST(KmlParser, ParseNonexistentFile) {
  GpsFixArray locations;
  std::string error;
  ASSERT_FALSE(KmlParser::parseFile("", &locations, &error));
  ASSERT_EQ(0U, locations.size());
  EXPECT_EQ(std::string("KML document not parsed successfully."), error);
}

char kEmptyKmlText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "</kml>";
TEST(KmlParser, ParseEmptyKmlFile) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlFile(&locations, kEmptyKmlText, &error));
  EXPECT_EQ(0U, locations.size());
  EXPECT_EQ("", error);
}

TEST(KmlParser, ParseEmptyKmlString) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlString(&locations, kEmptyKmlText, &error));
  EXPECT_EQ(0U, locations.size());
  EXPECT_EQ("", error);
}

char kValidkmlText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseValidKmlFile) {
  GpsFixArray locations;
  std::string error;

  ASSERT_TRUE(ParseKmlFile(&locations, kValidkmlText, &error));
  EXPECT_EQ(1U, locations.size());
  EXPECT_EQ("", error);
}

TEST(KmlParser, ParseValidKmlString) {
  GpsFixArray locations;
  std::string error;

  ASSERT_TRUE(ParseKmlString(&locations, kValidkmlText, &error));
  EXPECT_EQ(1U, locations.size());
  EXPECT_EQ("", error);
}

char kValidComplexText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Document>"
    "<name>KML Samples</name>"
    "<Style id=\"globeIcon\">"
    "<IconStyle></IconStyle><LineStyle><width>2</width></LineStyle>"
    "</Style>"
    "<Folder>"
    "<name>Placemarks</name>"
    "<description>These are just some</description>"
    "<LookAt>"
    "<tilt>40.5575073395506</tilt><range>500.6566641072245</range>"
    "</LookAt>"
    "<Placemark>"
    "<name>Tessellated</name>"
    "<visibility>0</visibility>"
    "<description>Black line (10 pixels wide), height tracks "
    "terrain</description>"
    "<LookAt><longitude>-122.0839597145766</longitude></LookAt>"
    "<styleUrl>#downArrowIcon</styleUrl>"
    "<Point>"
    "<altitudeMode>relativeToGround</altitudeMode>"
    "<coordinates>-122.084075,37.4220033612141,50</coordinates>"
    "</Point>"
    "</Placemark>"
    "<Placemark>"
    "<name>Transparent</name>"
    "<visibility>0</visibility>"
    "<styleUrl>#transRedPoly</styleUrl>"
    "<Polygon>"
    "<extrude>1</extrude>"
    "<altitudeMode>relativeToGround</altitudeMode>"
    "<outerBoundaryIs>"
    "<LinearRing>"
    "<coordinates> -122.084075,37.4220033612141,50</coordinates>"
    "</LinearRing>"
    "</outerBoundaryIs>"
    "</Polygon>"
    "</Placemark>"
    "</Folder>"
    "<Placemark>"
    "<name>Fruity</name>"
    "<visibility>0</visibility>"
    "<description><![CDATA[If the <tessellate> tag has a value of "
    "n]]></description>"
    "<LookAt><longitude>-112.0822680013139</longitude></LookAt>"
    "<LineString>"
    "<tessellate>1</tessellate>"
    "<coordinates> -122.084075,37.4220033612141,50 </coordinates>"
    "</LineString>"
    "</Placemark>"
    "</Document>"
    "</kml>";
TEST(KmlParser, ParseValidComplexFile) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlFile(&locations, kValidComplexText, &error));

  EXPECT_EQ("", error);
  EXPECT_EQ(3U, locations.size());

  EXPECT_EQ("Tessellated", locations[0].name);
  EXPECT_EQ("Black line (10 pixels wide), height tracks terrain",
            locations[0].description);
  EXPECT_EQ("Transparent", locations[1].name);
  EXPECT_EQ("", locations[1].description);
  EXPECT_EQ("Fruity", locations[2].name);
  EXPECT_EQ("If the <tessellate> tag has a value of n",
            locations[2].description);

  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_FLOAT_EQ(-122.084075, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.4220033612141, locations[i].latitude);
    EXPECT_FLOAT_EQ(50, locations[i].elevation);
  }
}

TEST(KmlParser, ParseValidComplexString) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlString(&locations, kValidComplexText, &error));

  EXPECT_EQ("", error);
  EXPECT_EQ(3U, locations.size());

  EXPECT_EQ("Tessellated", locations[0].name);
  EXPECT_EQ("Black line (10 pixels wide), height tracks terrain",
            locations[0].description);
  EXPECT_EQ("Transparent", locations[1].name);
  EXPECT_EQ("", locations[1].description);
  EXPECT_EQ("Fruity", locations[2].name);
  EXPECT_EQ("If the <tessellate> tag has a value of n",
            locations[2].description);

  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_FLOAT_EQ(-122.084075, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.4220033612141, locations[i].latitude);
    EXPECT_FLOAT_EQ(50, locations[i].elevation);
  }
}

char kOneCoordinateText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseOneCoordinateFile) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlFile(&locations, kOneCoordinateText, &error));

  EXPECT_EQ("", error);
  EXPECT_EQ(1U, locations.size());
  EXPECT_FLOAT_EQ(-122.0822035425683, locations[0].longitude);
  EXPECT_FLOAT_EQ(37.42228990140251, locations[0].latitude);
  EXPECT_FLOAT_EQ(0.0, locations[0].elevation);
}

TEST(KmlParser, ParseOneCoordinateString) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlString(&locations, kOneCoordinateText, &error));

  EXPECT_EQ("", error);
  EXPECT_EQ(1U, locations.size());
  EXPECT_FLOAT_EQ(-122.0822035425683, locations[0].longitude);
  EXPECT_FLOAT_EQ(37.42228990140251, locations[0].latitude);
  EXPECT_FLOAT_EQ(0.0, locations[0].elevation);
}

char kMultipleCoordinatesText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<LineString>"
    "<coordinates>-122.0822035425683,37.42228990140251,0 \
                  10.4,39.,20\t\t0,21.4,1"
    "</coordinates>"
    "</LineString>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseMultipleCoordinatesFile) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlFile(&locations, kMultipleCoordinatesText, &error));

  EXPECT_EQ("", error);
  ASSERT_EQ(3U, locations.size());

  EXPECT_FLOAT_EQ(-122.0822035425683, locations[0].longitude);
  EXPECT_FLOAT_EQ(37.42228990140251, locations[0].latitude);
  EXPECT_FLOAT_EQ(0, locations[0].elevation);
  EXPECT_FLOAT_EQ(10.4, locations[1].longitude);
  EXPECT_FLOAT_EQ(39., locations[1].latitude);
  EXPECT_FLOAT_EQ(20, locations[1].elevation);
  EXPECT_FLOAT_EQ(0, locations[2].longitude);
  EXPECT_FLOAT_EQ(21.4, locations[2].latitude);
  EXPECT_FLOAT_EQ(1, locations[2].elevation);
}

TEST(KmlParser, ParseMultipleCoordinatesString) {
  GpsFixArray locations;
  std::string error;
  ASSERT_TRUE(ParseKmlString(&locations, kMultipleCoordinatesText, &error));

  EXPECT_EQ("", error);
  ASSERT_EQ(3U, locations.size());

  EXPECT_FLOAT_EQ(-122.0822035425683, locations[0].longitude);
  EXPECT_FLOAT_EQ(37.42228990140251, locations[0].latitude);
  EXPECT_FLOAT_EQ(0, locations[0].elevation);
  EXPECT_FLOAT_EQ(10.4, locations[1].longitude);
  EXPECT_FLOAT_EQ(39., locations[1].latitude);
  EXPECT_FLOAT_EQ(20, locations[1].elevation);
  EXPECT_FLOAT_EQ(0, locations[2].longitude);
  EXPECT_FLOAT_EQ(21.4, locations[2].latitude);
  EXPECT_FLOAT_EQ(1, locations[2].elevation);
}

char kBadCoordinatesText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<LineString>"
    "<coordinates>-122.0822035425683, 37.42228990140251, 0 \
                  10.4,39.20\t021.41"
    "</coordinates>"
    "</LineString>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseBadCoordinatesFile) {
  GpsFixArray locations;
  std::string error;
  ASSERT_FALSE(ParseKmlFile(&locations, kBadCoordinatesText, &error));
}

TEST(KmlParser, ParseBadCoordinatesString) {
  GpsFixArray locations;
  std::string error;
  ASSERT_FALSE(ParseKmlString(&locations, kBadCoordinatesText, &error));
}

char kLocationNormalText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseLocationNormalFile) {
  GpsFixArray locations;
  std::string error;

  ASSERT_TRUE(ParseKmlFile(&locations, kLocationNormalText, &error));

  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_EQ("Simple placemark", locations[i].name);
    EXPECT_EQ("Attached to the ground.", locations[i].description);
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

TEST(KmlParser, ParseLocationNormalString) {
  GpsFixArray locations;
  std::string error;

  ASSERT_TRUE(ParseKmlString(&locations, kLocationNormalText, &error));

  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_EQ("Simple placemark", locations[i].name);
    EXPECT_EQ("Attached to the ground.", locations[i].description);
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

char kLocationMissingFieldsText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseLocationNormalMissingOptionalFieldsFile) {
  GpsFixArray locations;
  std::string error;

  ASSERT_TRUE(ParseKmlFile(&locations, kLocationMissingFieldsText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(1U, locations.size());
  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_EQ("", locations[i].name);
    EXPECT_EQ("", locations[i].description);
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

TEST(KmlParser, ParseLocationNormalMissingOptionalFieldsString) {
  GpsFixArray locations;
  std::string error;

  ASSERT_TRUE(ParseKmlString(&locations, kLocationMissingFieldsText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(1U, locations.size());
  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_EQ("", locations[i].name);
    EXPECT_EQ("", locations[i].description);
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

char kLocationMissingRequiredFieldsText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseLocationMissingRequiredFieldsFile) {
  GpsFixArray locations;
  std::string error;

  ASSERT_FALSE(
      ParseKmlFile(&locations, kLocationMissingRequiredFieldsText, &error));
  EXPECT_EQ("Location found with missing or malformed coordinates", error);
}

TEST(KmlParser, ParseLocationMissingRequiredFieldsString) {
  GpsFixArray locations;
  std::string error;

  ASSERT_FALSE(
      ParseKmlString(&locations, kLocationMissingRequiredFieldsText, &error));
  EXPECT_EQ("Location found with missing or malformed coordinates", error);
}

char kLocationNameOnlyFirstText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<name>Simple placemark</name>kk0"
    "<description>Attached to the ground.</description>"
    "<LineString>"
    "<coordinates>-122.0822035425683,37.42228990140251,0\
                  -122.0822035425683,37.42228990140251,0</coordinates>"
    "</LineString>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseLocationNameOnlyFirstFile) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlFile(&locations, kLocationNameOnlyFirstText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(2U, locations.size());

  EXPECT_EQ("Simple placemark", locations[0].name);
  EXPECT_EQ("Attached to the ground.", locations[0].description);
  EXPECT_EQ("", locations[1].name);
  EXPECT_EQ("", locations[1].description);

  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

TEST(KmlParser, ParseLocationNameOnlyFirstString) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlString(&locations, kLocationNameOnlyFirstText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(2U, locations.size());

  EXPECT_EQ("Simple placemark", locations[0].name);
  EXPECT_EQ("Attached to the ground.", locations[0].description);
  EXPECT_EQ("", locations[1].name);
  EXPECT_EQ("", locations[1].description);

  for (unsigned i = 0; i < locations.size(); ++i) {
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

char kMultipleLocationsText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0\
                  -122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParseMultipleLocationsFile) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlFile(&locations, kMultipleLocationsText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(4U, locations.size());

  for (unsigned i = 0; i < locations.size(); ++i) {
    if (i != 2) {
      EXPECT_EQ("Simple placemark", locations[i].name);
      EXPECT_EQ("Attached to the ground.", locations[i].description);
    } else {
      EXPECT_EQ("", locations[i].name);
      EXPECT_EQ("", locations[i].description);
    }
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

TEST(KmlParser, ParseMultipleLocationsString) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlString(&locations, kMultipleLocationsText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(4U, locations.size());

  for (unsigned i = 0; i < locations.size(); ++i) {
    if (i != 2) {
      EXPECT_EQ("Simple placemark", locations[i].name);
      EXPECT_EQ("Attached to the ground.", locations[i].description);
    } else {
      EXPECT_EQ("", locations[i].name);
      EXPECT_EQ("", locations[i].description);
    }
    EXPECT_FLOAT_EQ(-122.0822035425683, locations[i].longitude);
    EXPECT_FLOAT_EQ(37.42228990140251, locations[i].latitude);
    EXPECT_FLOAT_EQ(0, locations[i].elevation);
  }
}

char kTraverseEmptyDocText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\"></kml>";
TEST(KmlParser, TraverseEmptyDocFile) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlFile(&locations, kTraverseEmptyDocText, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(0U, locations.size());
}

TEST(KmlParser, TraverseEmptyDocString) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlString(&locations, kTraverseEmptyDocText, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(0U, locations.size());
}

char kNoPlacemarksText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<LineString></LineString>"
    "<name></name>"
    "</kml>";
TEST(KmlParser, TraverseDocNoPlacemarksFile) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlFile(&locations, kNoPlacemarksText, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(0U, locations.size());
}

TEST(KmlParser, TraverseDocNoPlacemarksString) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlString(&locations, kNoPlacemarksText, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(0U, locations.size());
}

char kNestedDocText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Document>"
    "<Folder>"
    "<name>Placemarks</name>"
    "<description>These are just some of the different kinds of placemarks "
    "with"
    "which you can mark your favorite places</description>"
    "<LookAt>"
    "<longitude>-122.0839597145766</longitude>"
    "<latitude>37.42222904525232</latitude>"
    "<altitude>0</altitude>"
    "<heading>-148.4122922628044</heading>"
    "<tilt>40.5575073395506</tilt>"
    "<range>500.6566641072245</range>"
    "</LookAt>"
    "<Placemark>"
    "<name>Simple placemark</name>"
    "<description>Attached to the ground.</description>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</Folder>"
    "</Document>"
    "</kml>";
TEST(KmlParser, TraverseNestedDocFile) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlFile(&locations, kNestedDocText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(1U, locations.size());

  EXPECT_EQ("Simple placemark", locations[0].name);
  EXPECT_EQ("Attached to the ground.", locations[0].description);
  EXPECT_FLOAT_EQ(-122.0822035425683, locations[0].longitude);
  EXPECT_FLOAT_EQ(37.42228990140251, locations[0].latitude);
  EXPECT_FLOAT_EQ(0, locations[0].elevation);
}

TEST(KmlParser, TraverseNestedDocString) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlString(&locations, kNestedDocText, &error));
  EXPECT_EQ("", error);
  ASSERT_EQ(1U, locations.size());

  EXPECT_EQ("Simple placemark", locations[0].name);
  EXPECT_EQ("Attached to the ground.", locations[0].description);
  EXPECT_FLOAT_EQ(-122.0822035425683, locations[0].longitude);
  EXPECT_FLOAT_EQ(37.42228990140251, locations[0].latitude);
  EXPECT_FLOAT_EQ(0, locations[0].elevation);
}

char kNullNameNoCrashText[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<kml xmlns=\"http://earth.google.com/kml/2.x\">"
    "<Placemark>"
    "<name/>"
    "<description/>"
    "<Point>"
    "<coordinates>-122.0822035425683,37.42228990140251,0</coordinates>"
    "</Point>"
    "</Placemark>"
    "</kml>";
TEST(KmlParser, ParsePlacemarkNullNameNoCrashFile) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlFile(&locations, kNullNameNoCrashText, &error));
  ASSERT_EQ(1U, locations.size());
  EXPECT_STREQ("", locations.front().name.c_str());
  EXPECT_STREQ("", locations.front().description.c_str());
}

TEST(KmlParser, ParsePlacemarkNullNameNoCrashString) {
  GpsFixArray locations;
  std::string error;
  EXPECT_TRUE(ParseKmlString(&locations, kNullNameNoCrashText, &error));
  ASSERT_EQ(1U, locations.size());
  EXPECT_STREQ("", locations.front().name.c_str());
  EXPECT_STREQ("", locations.front().description.c_str());
}

}  // namespace cuttlefish
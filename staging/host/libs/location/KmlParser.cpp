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

#include "KmlParser.h"
#include <libxml/parser.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <utility>
#include "StringParse.h"
using std::string;

// Coordinates can be nested arbitrarily deep within a Placemark, depending
// on the type of object (Point, LineString, Polygon) the Placemark contains
static xmlNode* findCoordinates(xmlNode* current) {
  for (; current != nullptr; current = current->next) {
    if (!strcmp((const char*)current->name, "coordinates")) {
      return current;
    }
    xmlNode* children = findCoordinates(current->xmlChildrenNode);
    if (children != nullptr) {
      return children;
    }
  }
  return nullptr;
}

// Coordinates have the following format:
//        <coordinates> -112.265654928602,36.09447672602546,2357
//                ...
//                -112.2657374587321,36.08646312301303,2357
//        </coordinates>
// often entirely contained in a single string, necessitating regex
static bool parseCoordinates(xmlNode* current, GpsFixArray* fixes) {
  xmlNode* coordinates_node = findCoordinates(current);
  bool result = true;
  if (coordinates_node == nullptr ||
      coordinates_node->xmlChildrenNode == nullptr ||
      coordinates_node->xmlChildrenNode->content == nullptr) {
    return false;
  }

  const char* coordinates =
      (const char*)(coordinates_node->xmlChildrenNode->content);
  int coordinates_len = strlen(coordinates);
  int offset = 0, n = 0;
  GpsFix new_fix;
  while (3 == SscanfWithCLocale(coordinates + offset, "%f , %f , %f%n",
                                &new_fix.longitude, &new_fix.latitude,
                                &new_fix.elevation, &n)) {
    fixes->push_back(new_fix);
    offset += n;
  }

  // Only allow whitespace at the end of the string to remain unconsumed.
  for (int i = offset; i < coordinates_len && result; ++i) {
    result = isspace(coordinates[i]);
  }

  return result;
}

static bool parseGxTrack(xmlNode* children, GpsFixArray* fixes) {
  bool result = true;
  for (xmlNode* current = children; result && current != nullptr;
       current = current->next) {
    if (current->ns && current->ns->prefix &&
        !strcmp((const char*)current->ns->prefix, "gx") &&
        !strcmp((const char*)current->name, "coord")) {
      std::string coordinates{(const char*)current->xmlChildrenNode->content};
      GpsFix new_fix;
      result = (3 == SscanfWithCLocale(coordinates.c_str(), "%f %f %f",
                                       &new_fix.longitude, &new_fix.latitude,
                                       &new_fix.elevation));
      fixes->push_back(new_fix);
    }
  }
  return result;
}

static bool parsePlacemark(xmlNode* current, GpsFixArray* fixes) {
  string description;
  string name;
  size_t ind = string::npos;
  // not worried about case-sensitivity since .kml files
  // are expected to be machine-generated
  for (; current != nullptr; current = current->next) {
    const bool hasContent =
        current->xmlChildrenNode && current->xmlChildrenNode->content;

    if (hasContent && !strcmp((const char*)current->name, "description")) {
      description = (const char*)current->xmlChildrenNode->content;
    } else if (hasContent && !strcmp((const char*)current->name, "name")) {
      name = (const char*)current->xmlChildrenNode->content;
    } else if (!strcmp((const char*)current->name, "Point") ||
               !strcmp((const char*)current->name, "LineString") ||
               !strcmp((const char*)current->name, "Polygon")) {
      ind = (ind != string::npos ? ind : fixes->size());
      if (!parseCoordinates(current->xmlChildrenNode, fixes)) {
        return false;
      }
    } else if (current->ns && current->ns->prefix &&
               !strcmp((const char*)current->ns->prefix, "gx") &&
               !strcmp((const char*)current->name, "Track")) {
      ind = (ind != string::npos ? ind : fixes->size());
      if (!parseGxTrack(current->xmlChildrenNode, fixes)) {
        return false;
      }
    }
  }

  if (ind == string::npos || ind >= fixes->size()) {
    return false;
  }

  // only assign name and description to the first of the
  // points to avoid needless repetition
  (*fixes)[ind].description = std::move(description);
  (*fixes)[ind].name = std::move(name);

  return true;
}

// Placemarks (aka locations) can be nested arbitrarily deep
static bool traverseSubtree(xmlNode* current, GpsFixArray* fixes,
                            string* error) {
  for (; current; current = current->next) {
    if (current->name != nullptr &&
        !strcmp((const char*)current->name, "Placemark")) {
      if (!parsePlacemark(current->xmlChildrenNode, fixes)) {
        *error = "Location found with missing or malformed coordinates";
        return false;
      }
    } else if (current->name != nullptr &&
               strcmp((const char*)current->name, "text") != 0) {
      // if it's not a Placemark we must go deeper
      if (!traverseSubtree(current->xmlChildrenNode, fixes, error)) {
        return false;
      }
    }
  }
  error->clear();
  return true;
}

bool KmlParser::parseFile(const char* filePath, GpsFixArray* fixes,
                          string* error) {
  // This initializes the library and checks potential ABI mismatches between
  // the version it was compiled for and the actual shared library used.
  LIBXML_TEST_VERSION

  xmlDocPtr doc = xmlReadFile(filePath, nullptr, 0);
  if (doc == nullptr) {
    *error = "KML document not parsed successfully.";
    xmlFreeDoc(doc);
    return false;
  }

  xmlNodePtr cur = xmlDocGetRootElement(doc);
  if (cur == nullptr) {
    *error = "Could not get root element of parsed KML file.";
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return false;
  }
  bool isWellFormed = traverseSubtree(cur, fixes, error);

  xmlFreeDoc(doc);
  xmlCleanupParser();

  return isWellFormed;
}

bool KmlParser::parseString(const char* str, int len, GpsFixArray* fixes,
                            string* error) {
  // This initializes the library and checks potential ABI mismatches between
  // the version it was compiled for and the actual shared library used.
  LIBXML_TEST_VERSION

  xmlDocPtr doc = xmlReadMemory(str, len, NULL, NULL, 0);
  if (doc == nullptr) {
    *error = "KML document not parsed successfully.";
    xmlFreeDoc(doc);
    return false;
  }

  xmlNodePtr cur = xmlDocGetRootElement(doc);
  if (cur == nullptr) {
    *error = "Could not get root element of parsed KML file.";
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return false;
  }
  bool isWellFormed = traverseSubtree(cur, fixes, error);

  xmlFreeDoc(doc);
  xmlCleanupParser();

  return isWellFormed;
}

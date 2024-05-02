/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/server_command/flags_collector.h"

#include <memory>

#include <android-base/logging.h>
#include "tinyxml2.h"

#include "common/libs/utils/contains.h"

using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

namespace cuttlefish {
namespace {

/*
 * Each "flag" xmlNode has child nodes such as file, name, meaning,
 * type, default, current, etc. Each child xmlNode is a leaf XML node,
 * which means that each child xmlNode has a child, and that child
 * keeps the value: e.g. the value of "name" xmlNode is the name
 * of the flag such as "daemon", "restart_subprocesses", etc.
 *
 * For the grandchild xmlNode of a flag xmlNode, we could do this
 * to retrieve the string value:
 *  xmlNodeListGetString(doc, grandchild, 1);
 */
FlagInfoPtr ParseFlagNode(const XMLDocument& doc, XMLElement& flag) {
  std::unordered_map<std::string, std::string> field_value_map;
  for (XMLElement* child = flag.FirstChildElement(); child != nullptr;
       child = child->NextSiblingElement()) {
    if (!child->Name()) {
      continue;
    }
    std::string field_name(child->Name());
    auto* xml_node_text = child->GetText();
    if (!xml_node_text) {
      field_value_map[field_name] = "";
      continue;
    }
    field_value_map[field_name] = xml_node_text;
  }
  if (field_value_map.empty()) {
    return nullptr;
  }
  return FlagInfo::Create(field_value_map);
}

std::vector<FlagInfoPtr> ParseXml(const XMLDocument& doc, XMLElement* node) {
  if (!node) {
    return {};
  }

  std::vector<FlagInfoPtr> flags;
  // if it is <flag> node
  if (node->Name() && node->Name() == std::string("flag")) {
    auto flag_info = ParseFlagNode(doc, *node);
    // we don't assume that a flag node is nested.
    if (flag_info) {
      flags.push_back(std::move(flag_info));
      return flags;
    }
    return {};
  }

  if (node->NoChildren()) {
    return {};
  }

  for (XMLElement* child_node = node->FirstChildElement();
       child_node != nullptr; child_node = child_node->NextSiblingElement()) {
    auto child_flags = ParseXml(doc, child_node);
    if (child_flags.empty()) {
      continue;
    }
    for (auto& child_flag : child_flags) {
      flags.push_back(std::move(child_flag));
    }
  }
  return flags;
}

std::unique_ptr<XMLDocument> BuildXmlDocFromString(const std::string& xml_str) {
  auto doc = std::make_unique<XMLDocument>();
  doc->Parse(xml_str.c_str());
  if (doc->ErrorID() != tinyxml2::XML_SUCCESS) {
    LOG(ERROR) << "helpxml parsing failed: " << xml_str;
    return nullptr;
  }
  return doc;
}

std::optional<std::vector<FlagInfoPtr>> LoadFromXml(std::unique_ptr<XMLDocument> doc) {
  std::vector<FlagInfoPtr> flags;
  auto* root = doc->RootElement();
  if (!root) {
    LOG(ERROR) << "Failed to get the root element from XML doc.";
    return std::nullopt;
  }
  flags = ParseXml(*doc, root);
  return flags;
}

}  // namespace

std::unique_ptr<FlagInfo> FlagInfo::Create(
    const FlagInfoFieldMap& field_value_map) {
  if (!Contains(field_value_map, "name") ||
      field_value_map.at("name").empty()) {
    return nullptr;
  }
  if (!Contains(field_value_map, "type") ||
      field_value_map.at("type").empty()) {
    return nullptr;
  }
  FlagInfo* new_flag_info = new FlagInfo(field_value_map);
  return std::unique_ptr<FlagInfo>(new_flag_info);
}

std::optional<std::vector<FlagInfoPtr>> CollectFlagsFromHelpxml(
    const std::string& xml_str) {
  auto helpxml_doc = BuildXmlDocFromString(xml_str);
  if (!helpxml_doc) {
    return std::nullopt;
  }
  return LoadFromXml(std::move(helpxml_doc));
}

}  // namespace cuttlefish

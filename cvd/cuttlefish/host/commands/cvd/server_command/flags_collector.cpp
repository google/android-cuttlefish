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

#include <android-base/logging.h>
#include <libxml/parser.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/scope_guard.h"

namespace cuttlefish {
namespace {

struct XmlDocDeleter {
  void operator()(struct _xmlDoc* doc);
};

using XmlDocPtr = std::unique_ptr<struct _xmlDoc, XmlDocDeleter>;

void XmlDocDeleter::operator()(struct _xmlDoc* doc) {
  if (!doc) {
    return;
  }
  xmlFree(doc);
}

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
FlagInfoPtr ParseFlagNode(struct _xmlDoc* doc, xmlNode& flag) {
  std::unordered_map<std::string, std::optional<std::string>> field_value_map;
  for (xmlNode* child = flag.xmlChildrenNode; child != nullptr;
       child = child->next) {
    if (!child->name) {
      continue;
    }
    std::string field_name = reinterpret_cast<const char*>(child->name);
    if (!child->xmlChildrenNode) {
      field_value_map[field_name] = std::nullopt;
      continue;
    }
    auto* xml_node_text = xmlNodeListGetString(doc, child->xmlChildrenNode, 1);
    if (!xml_node_text) {
      field_value_map[field_name] = "";
      continue;
    }
    field_value_map[field_name] = reinterpret_cast<const char*>(xml_node_text);
  }
  if (field_value_map.empty()) {
    return nullptr;
  }
  return FlagInfo::Create(field_value_map);
}

// root must not be nullptr
std::vector<FlagInfoPtr> ParseXml(struct _xmlDoc* doc, xmlNode* all_flags) {
  std::vector<FlagInfoPtr> flags;
  for (xmlNode* flag = all_flags->xmlChildrenNode; flag != nullptr;
       flag = flag->next) {
    if (!flag || !flag->name ||
        xmlStrcmp(flag->name, reinterpret_cast<const xmlChar*>("flag")) != 0) {
      continue;
    }
    auto flag_info = ParseFlagNode(doc, *flag);
    if (flag_info) {
      flags.emplace_back(std::move(flag_info));
    }
  }
  return flags;
}

XmlDocPtr BuildXmlDocFromString(const std::string& xml_str) {
  struct _xmlDoc* doc =
      xmlReadMemory(xml_str.data(), xml_str.size(), NULL, NULL, 0);
  XmlDocPtr doc_uniq_ptr = XmlDocPtr(doc, XmlDocDeleter());
  if (!doc) {
    LOG(ERROR) << "helpxml parsing failed.";
    return nullptr;
  }
  return doc_uniq_ptr;
}

std::optional<std::vector<FlagInfoPtr>> LoadFromXml(XmlDocPtr&& doc) {
  std::vector<FlagInfoPtr> flags;
  ScopeGuard exit_action([]() { xmlCleanupParser(); });
  {
    XmlDocPtr moved_doc = std::move(doc);
    xmlNode* root = xmlDocGetRootElement(moved_doc.get());
    if (!root) {
      LOG(ERROR) << "Failed to get the root element from XML doc.";
      return std::nullopt;
    }
    flags = ParseXml(moved_doc.get(), root);
  }
  return flags;
}

}  // namespace

std::unique_ptr<FlagInfo> FlagInfo::Create(
    const FlagInfoFieldMap& field_value_map) {
  if (!Contains(field_value_map, "name") ||
      field_value_map.at("name").value_or("").empty()) {
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

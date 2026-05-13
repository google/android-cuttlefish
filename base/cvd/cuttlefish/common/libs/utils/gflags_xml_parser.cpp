//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/common/libs/utils/gflags_xml_parser.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <libxml/parser.h>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::vector<GflagDescription>> ParseGflagsXmlHelp(
    std::string_view xml_help) {
  std::unique_ptr<xmlDoc, void (*)(xmlDocPtr)> doc(
      xmlReadMemory(xml_help.data(), xml_help.size(), nullptr, nullptr, 0),
      xmlFreeDoc);
  CF_EXPECT(doc != nullptr, "Failed to parse XML memory");

  xmlNodePtr root_element = xmlDocGetRootElement(doc.get());
  CF_EXPECT(root_element != nullptr, "XML document has no root element");
  CF_EXPECT(std::string((char*)root_element->name) == "AllFlags",
            "Root element is not AllFlags");

  std::vector<GflagDescription> flags;
  for (auto child = root_element->children; child != nullptr;
       child = child->next) {
    if (child->type != XML_ELEMENT_NODE) {
      continue;
    }
    if (std::string((char*)child->name) != "flag") {
      continue;
    }

    GflagDescription& flag_desc = flags.emplace_back();
    for (auto flag_child = child->children; flag_child != nullptr;
         flag_child = flag_child->next) {
      if (flag_child->type != XML_ELEMENT_NODE) {
        continue;
      }
      std::string tag_name((char*)flag_child->name);
      std::string content;
      if (flag_child->children != nullptr &&
          flag_child->children->type == XML_TEXT_NODE) {
        content = flag_child->children->content
                      ? (char*)flag_child->children->content
                      : "";
      }

      if (tag_name == "file") {
        flag_desc.file = content;
      } else if (tag_name == "name") {
        flag_desc.name = content;
      } else if (tag_name == "meaning") {
        flag_desc.meaning = content;
      } else if (tag_name == "default") {
        flag_desc.default_value = content;
      } else if (tag_name == "current") {
        flag_desc.current_value = content;
      } else if (tag_name == "type") {
        flag_desc.type = content;
      }
    }
  }
  return flags;
}

}  // namespace cuttlefish

/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "host/commands/assemble_cvd/flag_feature.h"

#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <gflags/gflags.h>
#include <string.h>
#include <string>
#include <unordered_set>
#include <vector>

#include "host/libs/config/feature.h"

namespace cuttlefish {

static std::string XmlEscape(const std::string& s) {
  using android::base::StringReplace;
  return StringReplace(StringReplace(s, "<", "&lt;", true), ">", "&gt;", true);
}

class ParseGflagsImpl : public ParseGflags {
 public:
  INJECT(ParseGflagsImpl(ConfigFlag& config)) : config_(config) {}

  std::string Name() const override { return "ParseGflags"; }
  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_)};
  }
  Result<void> Process(std::vector<std::string>& args) override {
    std::string process_name = "assemble_cvd";
    std::vector<char*> pseudo_argv = {process_name.data()};
    for (auto& arg : args) {
      pseudo_argv.push_back(arg.data());
    }
    int argc = pseudo_argv.size();
    auto argv = pseudo_argv.data();
    gflags::AllowCommandLineReparsing();  // Support future non-gflags flags
    gflags::ParseCommandLineNonHelpFlags(&argc, &argv,
                                         /* remove_flags */ false);
    return {};
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    // Lifted from external/gflags/src/gflags_reporting.cc:ShowXMLOfFlags
    std::vector<gflags::CommandLineFlagInfo> flags;
    gflags::GetAllFlags(&flags);
    for (const auto& flag : flags) {
      // From external/gflags/src/gflags_reporting.cc:DescribeOneFlagInXML
      out << "<flag>\n";
      out << "  <file>" << XmlEscape(flag.filename) << "</file>\n";
      out << "  <name>" << XmlEscape(flag.name) << "</name>\n";
      out << "  <meaning>" << XmlEscape(flag.description) << "</meaning>\n";
      out << "  <default>" << XmlEscape(flag.default_value) << "</default>\n";
      out << "  <current>" << XmlEscape(flag.current_value) << "</current>\n";
      out << "  <type>" << XmlEscape(flag.type) << "</type>\n";
      out << "</flag>\n";
    }
    return true;
  }

 private:
  ConfigFlag& config_;
};

fruit::Component<fruit::Required<ConfigFlag>, ParseGflags> GflagsComponent() {
  return fruit::createComponent()
      .bind<ParseGflags, ParseGflagsImpl>()
      .addMultibinding<FlagFeature, ParseGflags>();
}

}  // namespace cuttlefish

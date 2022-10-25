/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/selector/selector_cvd_flags.h"

#include <regex>

#include <android-base/strings.h>

#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"

namespace cuttlefish {
namespace selector {

Result<SelectorFlagsParser> SelectorFlagsParser::ConductSelectFlagsParser(
    const std::vector<std::string>& args) {
  SelectorFlagsParser parser(args);
  CF_EXPECT(parser.Parse(), "selector option flag parsing failed.");
  return {std::move(parser)};
}

SelectorFlagsParser::SelectorFlagsParser(const std::vector<std::string>& args)
    : args_(args) {}

bool SelectorFlagsParser::HasName() const {
  return (opt_value_map_.find(kNameOpt) != opt_value_map_.end() &&
          !opt_value_map_.at(kNameOpt).empty());
}

std::string SelectorFlagsParser::Name() const {
  if (!HasName()) {
    return "";
  }
  return opt_value_map_.at(kNameOpt);
}

Result<void> SelectorFlagsParser::Parse() {
  // register selector flags
  std::string name;
  std::vector<Flag> flags;
  flags.emplace_back(GflagsCompatFlag(kNameOpt, name));
  CF_EXPECT(ParseFlags(flags, args_));

  if (!name.empty()) {
    CF_EXPECT(IsNameValid(name));
    opt_value_map_[kNameOpt] = name;
  }

  if (args_.empty()) {
    return {};
  }
  substring_queries_ =
      CF_EXPECT(FindSubstringsToMatch(),
                "Selector flags has positional arguments, and they must be "
                    << "substring match, which is not the case.");
  return {};
}

/*
 * The remaining arguments must be like:
 *  ?substr0 ?substr1,substr2,subtr3 ...
 */
Result<std::unordered_set<std::string>>
SelectorFlagsParser::FindSubstringsToMatch() {
  std::unordered_set<std::string> substring_queries;
  const auto args_size = args_.size();
  for (int i = 0; i < args_size; i++) {
    /*
     * Logically, the order does not matter. The reason why we start from
     * behind is that pop_back() of a vector is much cheaper than pop_front()
     */
    const auto& substring = args_.back();
    auto tokens = android::base::Split(substring, ",");
    for (const auto& t : tokens) {
      if (t.empty()) {
        continue;
      }
      substring_queries.insert(t);
    }
    args_.pop_back();
  }
  return {substring_queries};
}

bool SelectorFlagsParser::IsNameValid(const std::string& name) const {
  return IsValidGroupName(name) || IsValidInstanceName(name) ||
         IsValidDeviceName(name);
}

}  // namespace selector
}  // namespace cuttlefish

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

#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "cuttlefish/flag_parser/flag.h"

namespace cuttlefish {

/* Write gflags `--helpxml` style output for a string-type flag. */
bool WriteGflagsCompatXml(const Flag&, std::ostream&);
bool WriteGflagsCompatXml(const std::vector<Flag>&, std::ostream&);

// Create a flag resembling a gflags argument of the given type. This includes
// "-[-]flag=*",support for all types, "-[-]noflag" support for booleans, and
// "-flag *", "--flag *", support for other types. The value passed in the flag
// is saved to the defined reference.
Flag GflagsCompatFlag(const std::string& name);
Flag GflagsCompatBoolFlag(const std::string& name);
Flag GflagsCompatFlag(const std::string& name, std::string& value);
Flag GflagsCompatFlag(const std::string& name, int32_t& value);
Flag GflagsCompatFlag(const std::string& name, size_t& value);
Flag GflagsCompatFlag(const std::string& name, bool& value);
Flag GflagsCompatFlag(const std::string& name, std::vector<std::string>& value);
Flag GflagsCompatFlag(const std::string& name, std::vector<bool>& value,
                      bool default_value);
// Indicates when to assign std::nullopt to the std::optional backing the flag.
enum class CoerceToNullopt {
  None, // When the flag is not present in the arguments
  UnsetKeyword, // When the flag has the "unset" special value.
  EmptyString, // When the flag has an empty value (`--flag "" or `--flag=`)
};
Flag GflagsCompatFlag(
    const std::string& name, std::optional<std::string>& value,
    CoerceToNullopt opt = CoerceToNullopt::None);
Flag GflagsCompatFlag(
    const std::string& name, std::optional<size_t>& value,
    CoerceToNullopt opt = CoerceToNullopt::None);
Flag GflagsCompatFlag(
    const std::string& name, std::optional<unsigned>& value,
    CoerceToNullopt opt = CoerceToNullopt::None);

Flag GflagsCompatFlag(
    const std::string& name, std::optional<std::vector<std::string>>& value,
    CoerceToNullopt opt = CoerceToNullopt::None);
Flag GflagsCompatFlag(
    const std::string& name, std::optional<std::vector<unsigned>>& value,
    CoerceToNullopt opt = CoerceToNullopt::None);

/* If a "-help" or "--help" flag is present, prints all the flags and fails. */
Flag HelpFlag(const std::vector<Flag>& flags, std::string text = "");

/* If a "-helpxml" is present, prints all the flags in XML and fails. */
Flag HelpXmlFlag(const std::vector<Flag>& flags, std::ostream&, bool& value,
                 std::string text = "");

}  // namespace cuttlefish

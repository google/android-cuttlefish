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

#include <functional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct ConsumeFlagsOpts;

class Flag {
 public:
  static Flag StringFlag(std::string name);
  static Flag BoolFlag(std::string name);

  /* Add an alias that triggers matches and calls to the `Setter` function. */
  Flag& Alias(std::string alias) &;
  Flag Alias(std::string alias) &&;
  /* Set help text, visible in the class ostream writer method. Optional. */
  Flag& Help(std::string) &;
  Flag Help(std::string) &&;
  /* Set the value name hint used in the synopsis. Optional. */
  Flag& ValueNameHint(std::string) &;
  Flag ValueNameHint(std::string) &&;
  /* Set a loader that displays the current value in help text. Optional. */
  Flag& Getter(std::function<std::string()>) &;
  Flag Getter(std::function<std::string()>) &&;
  /* Set the callback for matches. The callback may be invoked multiple times.
   */
  Flag& Setter(std::function<Result<void>(std::string_view)>) &;
  Flag Setter(std::function<Result<void>(std::string_view)>) &&;
  /* Add a callback to validate the parsed flag value. These callbacks are
   * guaranteed to be called after Setter succeeds in the same order they are
   * added. Validation stops when one validator callback fails, remaining
   * callbacks are not executed. */
  Flag& AddValidator(std::function<Result<void>()>) &;
  Flag AddValidator(std::function<Result<void>()>) &&;

  std::string Name() const;
  bool operator<(const Flag& other) const { return Name() < other.Name(); }
  std::string Synopsis() const;
  const std::string& Description() const;
  std::string CurrentValue() const;
  const std::string& ValueNameHint() const;

 private:
  enum class Style {
    String,
    Bool,
  };
  Flag(std::string name, Style style);

  void ValidateAlias(const std::string&);

  /* Calls the flag's setter with the appropriate value if it matches the
   * current argument. Returns the number of arguments consumed by the flag
   * (typically between 0 and 2), with 0 indicating the flag doesn't match. */
  Result<size_t> Match(std::string_view current_arg,
                       std::span<const std::string> following_args) const;
  Result<size_t> MatchStringStyleFlag(std::string_view,
                                      std::span<const std::string>) const;
  Result<size_t> MatchBoolStyleFlag(std::string_view) const;
  Result<void> SetAndValidate(std::string_view) const;

  friend void WriteGflagsCompatXml(const Flag&, std::ostream&);
  friend Result<void> ConsumeFlags(const std::vector<Flag>&,
                                   std::vector<std::string>&, ConsumeFlagsOpts);

  std::vector<std::string> aliases_;
  Style style_;
  std::string help_;
  std::string value_name_hint_ = "VAL";
  std::function<std::string()> getter_ = []() { return ""; };
  std::function<Result<void>(std::string_view value)> setter_;
  std::vector<std::function<Result<void>()>> validators_;
};

std::ostream& operator<<(std::ostream&, const Flag&);

struct ConsumeFlagsOpts {
  /* Stop matching flags when the "--" separator is found. The other options
   * only apply to the arguments up to but exluding the "--", if present. */
  bool stop_at_double_dashes = false;
  /* Fail if there are unconsumed arguments left. */
  bool fail_on_unexpected_argument = false;
};
/* Matches flags against a list of arguments, removing all matches and leaving
 * only unmatched arguments.
 */
Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>& args,
                          ConsumeFlagsOpts opts = {});
Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>&& args,
                          ConsumeFlagsOpts opts = {});

}  // namespace cuttlefish

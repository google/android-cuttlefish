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
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

/* The matching behavior used with the name in `FlagAlias::name`. */
enum class FlagAliasMode {
  /* Match arguments of the form `<name><value>`. In practice, <name> usually
   * looks like "-flag=" or "--flag=", where the "-" and "=" are included in
   * parsing. */
  kFlagPrefix,
  /* Match arguments of the form `<name>`. In practice, <name> will look like
   * "-flag" or "--flag". */
  kFlagExact,
  /* Match a pair of arguments of the form `<name>` `<value>`. In practice,
   * <name> will look like "-flag" or "--flag". */
  kFlagConsumesFollowing,
};

/* A single matching rule for a `Flag`. One `Flag` can have multiple rules. */
struct FlagAlias {
  FlagAliasMode mode;
  std::string name;
};

std::ostream& operator<<(std::ostream&, const FlagAlias&);

/* A successful match in an argument list from a `FlagAlias` inside a `Flag`.
 * The `key` value corresponds to `FlagAlias::name`. For a match of
 * `FlagAliasMode::kFlagExact`, `key` and `value` will both be the `name`. */
struct FlagMatch {
  std::string key;
  std::string value;
};

class Flag {
 public:
  explicit Flag(std::string name) : name_(std::move(name)) {}

  /* Add an alias that triggers matches and calls to the `Setter` function. */
  Flag& Alias(const FlagAlias& alias) &;
  Flag Alias(const FlagAlias& alias) &&;
  /* Set help text, visible in the class ostream writer method. Optional. */
  Flag& Help(const std::string&) &;
  Flag Help(const std::string&) &&;
  /* Set a loader that displays the current value in help text. Optional. */
  Flag& Getter(std::function<std::string()>) &;
  Flag Getter(std::function<std::string()>) &&;
  /* Set the callback for matches. The callback may be invoked multiple times.
   */
  Flag& Setter(std::function<Result<void>(const FlagMatch&)>) &;
  Flag Setter(std::function<Result<void>(const FlagMatch&)>) &&;
  /* Add a callback to validate the parsed flag value. These callbacks are
   * guaranteed to be called after Setter succeeds in the same order they are
   * added. Validation stops when one validator callback fails, remaining
   * callbacks are not executed.
   */
  Flag& AddValidator(std::function<Result<void>()>) &;
  Flag AddValidator(std::function<Result<void>()>) &&;

  const std::string& Name() const { return name_; }
  bool operator<(const Flag& other) const { return Name() < other.Name(); }

  /* Examines a list of arguments, removing any matches from the list and
   * invoking the `Setter` for every match. Returns `false` if the callback ever
   * returns `false`. Non-matches are left in place. */
  Result<void> Parse(std::vector<std::string>& arguments) const;
  Result<void> Parse(std::vector<std::string>&& arguments) const;

 private:
  /* Write gflags `--helpxml` style output for a string-type flag. */
  friend bool WriteGflagsCompatXml(const Flag&, std::ostream&);

  /* Reports whether `Process` wants to consume zero, one, or two arguments. */
  enum class FlagProcessResult {
    /* Error in handling a flag, exit flag handling with an error result. */
    kFlagSkip,                  /* Flag skipped; consume no arguments. */
    kFlagConsumed,              /* Flag processed; consume one argument. */
    kFlagConsumedWithFollowing, /* Flag processed; consume 2 arguments. */
  };

  void ValidateAlias(const FlagAlias& alias);
  Flag& UnvalidatedAlias(const FlagAlias& alias) &;
  Flag UnvalidatedAlias(const FlagAlias& alias) &&;

  /* Attempt to match a single argument. */
  Result<FlagProcessResult> Process(
      const std::string& argument,
      const std::optional<std::string>& next_arg) const;
  Result<void> SetAndValidate(const FlagMatch&) const;

  bool HasAlias(const FlagAlias&) const;

  friend std::ostream& operator<<(std::ostream&, const Flag&);
  friend Flag InvalidFlagGuard();
  friend Flag UnexpectedArgumentGuard();

  friend Result<void> ConsumeFlagsConstrained(const std::vector<Flag>& flags,
                                              std::vector<std::string>&);

  std::string name_;
  std::vector<FlagAlias> aliases_;
  std::optional<std::string> help_;
  std::optional<std::function<std::string()>> getter_;
  std::optional<std::function<Result<void>(const FlagMatch&)>> setter_;
  std::vector<std::function<Result<void>()>> validators_;
};

std::ostream& operator<<(std::ostream&, const Flag&);

/* Catches any argument that begin with `-` and errors out. When used after all
 * valid flags it effectively fails on unrecognized flags.
 */
Flag InvalidFlagGuard();
/* Catches any arguments not extracted by other Flag matchers and errors out.
 * This effectively denies unknown flags and any positional arguments. */
Flag UnexpectedArgumentGuard();

/* Handles a list of flags. Flags are matched in the order given in case two
 * flags match the same argument. Matched flags are removed, leaving only
 * unmatched arguments. */
Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>& args,
                          bool recognize_end_of_option_mark = false);
Result<void> ConsumeFlags(const std::vector<Flag>& flags,
                          std::vector<std::string>&&,
                          bool recognize_end_of_option_mark = false);

/* Handles a list of flags. Arguments are handled from the beginning. When an
 * unrecognized argument is encountered, parsing stops. At most one flag matcher
 * can handle a particular argument. */
Result<void> ConsumeFlagsConstrained(const std::vector<Flag>& flags,
                                     std::vector<std::string>&);
Result<void> ConsumeFlagsConstrained(const std::vector<Flag>& flags,
                                     std::vector<std::string>&&);

}  // namespace cuttlefish

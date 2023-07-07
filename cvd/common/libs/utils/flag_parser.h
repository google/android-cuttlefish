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

#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"

/* Support for parsing individual flags out of a larger list of flags. This
 * supports externally determining the order that flags are evaluated in, and
 * incrementally integrating with existing flag parsing implementations.
 *
 * Start with Flag() or one of the GflagsCompatFlag(...) functions to create new
 * flags. These flags should be aggregated through the application through some
 * other mechanism and then evaluated individually with Flag::Parse or together
 * with ParseFlags on arguments. */

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
  /* Match a sequence of arguments of the form `<name>` `<value>` `<value>`.
   * This uses heuristics to try to determine when `<value>` is actually another
   * flag. */
  kFlagConsumesArbitrary,
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
  /* Add an alias that triggers matches and calls to the `Setter` function. */
  Flag& Alias(const FlagAlias& alias) &;
  Flag Alias(const FlagAlias& alias) &&;
  /* Set help text, visible in the class ostream writer method. Optional. */
  Flag& Help(const std::string&) &;
  Flag Help(const std::string&) &&;
  /* Set a loader that displays the current value in help text. Optional. */
  Flag& Getter(std::function<std::string()>) &;
  Flag Getter(std::function<std::string()>) &&;
  /* Set the callback for matches. The callback be invoked multiple times. */
  Flag& Setter(std::function<bool(const FlagMatch&)>) &;
  Flag Setter(std::function<bool(const FlagMatch&)>) &&;

  /* Examines a list of arguments, removing any matches from the list and
   * invoking the `Setter` for every match. Returns `false` if the callback ever
   * returns `false`. Non-matches are left in place. */
  bool Parse(std::vector<std::string>& flags) const;
  bool Parse(std::vector<std::string>&& flags) const;

  /* Write gflags `--helpxml` style output for a string-type flag. */
  bool WriteGflagsCompatXml(std::ostream&) const;

 private:
  /* Reports whether `Process` wants to consume zero, one, or two arguments. */
  enum class FlagProcessResult {
    /* Error in handling a flag, exit flag handling with an error result. */
    kFlagError,
    kFlagSkip,                  /* Flag skipped; consume no arguments. */
    kFlagConsumed,              /* Flag processed; consume one argument. */
    kFlagConsumedWithFollowing, /* Flag processed; consume 2 arguments. */
    kFlagConsumedOnlyFollowing, /* Flag processed; consume next argument. */
  };

  void ValidateAlias(const FlagAlias& alias);
  Flag& UnvalidatedAlias(const FlagAlias& alias) &;
  Flag UnvalidatedAlias(const FlagAlias& alias) &&;

  /* Attempt to match a single argument. */
  FlagProcessResult Process(const std::string& argument,
                            const std::optional<std::string>& next_arg) const;

  bool HasAlias(const FlagAlias&) const;

  friend std::ostream& operator<<(std::ostream&, const Flag&);
  friend Flag InvalidFlagGuard();
  friend Flag UnexpectedArgumentGuard();

  std::vector<FlagAlias> aliases_;
  std::optional<std::string> help_;
  std::optional<std::function<std::string()>> getter_;
  std::optional<std::function<bool(const FlagMatch&)>> setter_;
};

std::ostream& operator<<(std::ostream&, const Flag&);

std::vector<std::string> ArgsToVec(int argc, char** argv);

Result<bool> ParseBool(const std::string& value, const std::string& name);

/* Handles a list of flags. Flags are matched in the order given in case two
 * flags match the same argument. Matched flags are removed, leaving only
 * unmatched arguments. */
bool ParseFlags(const std::vector<Flag>& flags, std::vector<std::string>& args,
                const bool recognize_end_of_option_mark = false);
bool ParseFlags(const std::vector<Flag>& flags, std::vector<std::string>&&,
                const bool recognize_end_of_option_mark = false);

bool WriteGflagsCompatXml(const std::vector<Flag>&, std::ostream&);

/* If -verbosity or --verbosity flags have a value, translates it to an android
 * LogSeverity */
Flag VerbosityFlag(android::base::LogSeverity& value);

/* If any of these are used, they should be evaluated after all other flags, and
 * in the order defined here (help before invalid flags, invalid flags before
 * unexpected arguments). */

/* If a "-help" or "--help" flag is present, prints all the flags and fails. */
Flag HelpFlag(const std::vector<Flag>& flags, const std::string& text = "");

/* If a "-helpxml" is present, prints all the flags in XML and fails. */
Flag HelpXmlFlag(const std::vector<Flag>& flags, std::ostream&, bool& value,
                 const std::string& text = "");

/* Catches unrecognized arguments that begin with `-`, and errors out. This
 * effectively denies unknown flags. */
Flag InvalidFlagGuard();
/* Catches any arguments not extracted by other Flag matchers and errors out.
 * This effectively denies unknown flags and any positional arguments. */
Flag UnexpectedArgumentGuard();

// Create a flag resembling a gflags argument of the given type. This includes
// "-[-]flag=*",support for all types, "-[-]noflag" support for booleans, and
// "-flag *", "--flag *", support for other types. The value passed in the flag
// is saved to the defined reference.
Flag GflagsCompatFlag(const std::string& name);
Flag GflagsCompatFlag(const std::string& name, std::string& value);
Flag GflagsCompatFlag(const std::string& name, std::int32_t& value);
Flag GflagsCompatFlag(const std::string& name, bool& value);
Flag GflagsCompatFlag(const std::string& name, std::vector<std::string>& value);
Flag GflagsCompatFlag(const std::string& name, std::vector<bool>& value,
                      const bool default_value);

}  // namespace cuttlefish

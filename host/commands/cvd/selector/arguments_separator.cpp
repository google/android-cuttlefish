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

#include "host/commands/cvd/selector/arguments_separator.h"

#include <deque>

#include <android-base/strings.h>

#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace selector {

Result<std::unique_ptr<ArgumentsSeparator>> ArgumentsSeparator::Parse(
    const FlagsRegistration& flag_registration,
    const std::vector<std::string>& input_args) {
  LexerFlagsSpecification lexer_flag_spec{
      .known_boolean_flags = flag_registration.known_boolean_flags,
      .known_value_flags = flag_registration.known_value_flags,
  };
  auto lexer = CF_EXPECT(ArgumentsLexerBuilder::Build(lexer_flag_spec));
  CF_EXPECT(lexer != nullptr);
  ArgumentsSeparator* new_arg_separator =
      new ArgumentsSeparator(std::move(lexer), input_args, flag_registration);
  CF_EXPECT(new_arg_separator != nullptr,
            "Memory allocation failed for ArgumentSeparator");
  std::unique_ptr<ArgumentsSeparator> arg_separator{new_arg_separator};
  CF_EXPECT(arg_separator->Parse());
  return std::move(arg_separator);
}

Result<std::unique_ptr<ArgumentsSeparator>> ArgumentsSeparator::Parse(
    const FlagsRegistration& flag_registration,
    const CvdProtobufArg& input_args) {
  std::vector<std::string> input_args_vec;
  input_args_vec.reserve(input_args.size());
  for (const auto& input_arg : input_args) {
    input_args_vec.emplace_back(input_arg);
  }
  auto arg_separator = CF_EXPECT(Parse(flag_registration, input_args_vec));
  return std::move(arg_separator);
}

Result<std::unique_ptr<ArgumentsSeparator>> ArgumentsSeparator::Parse(
    const FlagsRegistration& flag_registration, const std::string& input_args,
    const std::string delim) {
  std::vector<std::string> input_args_vec =
      android::base::Tokenize(input_args, delim);
  auto arg_separator = CF_EXPECT(Parse(flag_registration, input_args_vec));
  return std::move(arg_separator);
}

ArgumentsSeparator::ArgumentsSeparator(
    std::unique_ptr<ArgumentsLexer>&& lexer,
    const std::vector<std::string>& input_args,
    const FlagsRegistration& flag_registration)
    : lexer_(std::move(lexer)),
      input_args_(input_args),
      known_boolean_flags_(flag_registration.known_boolean_flags),
      known_value_flags_(flag_registration.known_value_flags),
      valid_subcmds_(flag_registration.valid_subcommands) {}

Result<void> ArgumentsSeparator::Parse() {
  auto output = CF_EXPECT(ParseInternal());
  prog_path_ = std::move(output.prog_path);
  cvd_args_ = std::move(output.cvd_args);
  sub_cmd_ = std::move(output.sub_cmd);
  sub_cmd_args_ = std::move(output.sub_cmd_args);
  return {};
}

Result<ArgumentsSeparator::Output> ArgumentsSeparator::ParseInternal() {
  CF_EXPECT(lexer_ != nullptr);
  CF_EXPECT(!input_args_.empty());
  Output output;
  return output;
}

}  // namespace selector
}  // namespace cuttlefish

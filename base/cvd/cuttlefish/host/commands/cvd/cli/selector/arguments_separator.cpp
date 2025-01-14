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

#include "host/commands/cvd/cli/selector/arguments_separator.h"

#include <deque>

#include <android-base/strings.h>

#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace selector {

/*
 * prog_name, <optional cvd flags>, sub_cmd, <optional sub_cmd flags>
 *
 * -- could be included, which makes things complicated. However, if -- is
 * part of cvd flags, it's ill-formatted. If -- is among sub_cmd flags,
 * we will just forward it.
 *
 * If something like this is really needed, use the suggested alternative:
 *    original: cvd --some_flag -- --this-is-value start --subcmd_args
 * alternative: cvd --some_flag="--this-is-value" start --subcmd_args
 *
 */
Result<SeparatedArguments> SeparateArguments(
    const std::vector<std::string>& input_args) {
  CF_EXPECT(!input_args.empty());

  std::vector<ArgToken> tokens_vec = CF_EXPECT(TokenizeArguments(input_args));
  std::deque<ArgToken> tokens_queue(tokens_vec.begin(), tokens_vec.end());

  // take program path/name
  CF_EXPECT(!tokens_queue.empty() &&
            tokens_queue.front().Type() == ArgType::kPositional);

  SeparatedArguments output;
  output.prog_path = std::move(tokens_queue.front().Token());
  tokens_queue.pop_front();

  // break loop either if there is no token or
  // the subcommand token is consumed
  bool cvd_flags_mode = true;
  while (!tokens_queue.empty() && cvd_flags_mode) {
    const auto current = std::move(tokens_queue.front());
    const auto current_type = current.Type();
    const auto& current_token = current.Token();
    tokens_queue.pop_front();

    // look up next if any
    std::optional<ArgToken> next;
    if (!tokens_queue.empty()) {
      next = tokens_queue.front();
    }

    switch (current_type) {
      case ArgType::kKnownValueFlag: {
        output.cvd_args.emplace_back(current_token);
        if (next && next->Type() == ArgType::kPositional) {
          output.cvd_args.emplace_back(next->Token());
          tokens_queue.pop_front();
        }
      } break;
      case ArgType::kKnownFlagAndValue:
      case ArgType::kKnownBoolFlag:
      case ArgType::kKnownBoolNoFlag: {
        output.cvd_args.emplace_back(current_token);
      } break;
      case ArgType::kPositional: {
        output.sub_cmd = current.Token();
        CF_EXPECT(output.sub_cmd != std::nullopt);
        cvd_flags_mode = false;
      } break;
      case ArgType::kDoubleDash: {
        return CF_ERR("`--` is not allowed within cvd specific flags.");
      }
      case ArgType::kUnknownFlag:
      case ArgType::kError: {
        return CF_ERR(current.Token()
                      << " in cvd-specific flags is disallowed.");
      }
    }
  }
  while (!tokens_queue.empty()) {
    auto token = std::move(tokens_queue.front().Token());
    output.sub_cmd_args.emplace_back(std::move(token));
    tokens_queue.pop_front();
  }
  return output;
}

}  // namespace selector
}  // namespace cuttlefish

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

#include "host/commands/cvd/selector/selector_cmdline_parser.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <iterator>
#include <stack>

namespace cuttlefish {
namespace selector {
namespace {

struct SeparatedArguments {
  std::vector<std::string> before_selector_opts;
  std::vector<std::string> selector_specific;
  std::vector<std::string> after_selector_opts;
};

enum class ParseState {
  kInit = 0,
  kSelector = 1,
  kAfterSelector = 2,
  kParseError = 3
};

/*
 * Basically, cmd with the arguments would look this:
 *  $ cvd [ <selector options> ] <cvd options>
 *
 * Note that there might be no space following '['. And, there might be
 * no preceding space before ']'. '[]' is not allowed to be nested: no other
 * '[]' pair inside '[]'.
 *
 * Valid examples are:
 *
 * $ cvd [--name my_device ] start --daemon
 *
 * Thie example will be separated into these three components:
 * { "cvd" },
 * { "--name", "my_device" },
 * { "start", "--daemon" }
 *
 * Essentially, we capture the first `[ * ]` if any, and take it as the
 * selector options. If ever there is another `[ * ]` pattern following
 * the first one, we just give put it into third list, which is going to
 * be given to cvd server as "args" in the protobuf.
 *
 * The implementation is a sort of state machine. In the initial state,
 * it will wait the opening '[', and if there comes one, the state is
 * transitioned to the kSelector state.
 *
 * In the kSelector state, the input arguments are saved in
 * selector_specific_arg. Also, it will wait the closing ']'. If there
 * comes one, the state is transitioned to kAfterSelector state.
 *
 * In the kAfterSelector state, whether the token includes [, ], or none of
 * any, everything is just saved as is in after_selector_opts.
 *
 */
Result<SeparatedArguments> SeparateArguments(
    const std::vector<std::string>& args_orig) {
  std::deque<std::string> args(args_orig.cbegin(), args_orig.cend());
  std::vector<std::string> before_selector_opts;
  std::vector<std::string> selector_specific;
  std::vector<std::string> after_selector_opts;
  ParseState state = ParseState::kInit;
  while (!args.empty()) {
    std::string arg = args.front();
    args.pop_front();

    switch (state) {
      case ParseState::kInit: {
        if (arg.empty()) {
          before_selector_opts.emplace_back(arg);
          break;
        }
        if (arg.front() != '[') {
          CF_EXPECT(arg.back() != ']', "Seletor option parse error: "
                                           << "] apears before [ is consumed.");
          before_selector_opts.emplace_back(arg);
          break;
        }

        if (arg != "[") {
          // remove the preceding [, and return it to the input queue
          arg = arg.substr(1);
          args.push_front(arg);
        }
        state = ParseState::kSelector;
      } break;

      case ParseState::kSelector: {
        CF_EXPECT(!arg.empty() && arg.front() != '[',
                  "Selector option parse error.");
        if (arg.back() != ']') {
          selector_specific.emplace_back(arg);
          break;
        }
        if (arg != "]") {
          selector_specific.emplace_back(arg.substr(0, arg.size() - 1));
        }
        state = ParseState::kAfterSelector;
      } break;

      case ParseState::kAfterSelector: {
        after_selector_opts.emplace_back(arg);
      } break;
      default:
        return CF_ERR("Selector option parsing error.");
    }
  }
  return {SeparatedArguments{.before_selector_opts = before_selector_opts,
                             .selector_specific = selector_specific,
                             .after_selector_opts = after_selector_opts}};
}

}  // namespace

Result<CommandAndSelectorArguments> GetCommandAndSelectorArguments(
    const std::vector<std::string>& args) {
  auto [pre, selector_specific, post] = CF_EXPECT(SeparateArguments(args));
  std::vector<std::string> cmd_args{pre};
  std::copy(post.begin(), post.end(), std::back_inserter(cmd_args));
  return CommandAndSelectorArguments{.cmd_args = cmd_args,
                                     .selector_args = selector_specific};
}

}  // namespace selector
}  // namespace cuttlefish

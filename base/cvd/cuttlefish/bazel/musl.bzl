# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Helpers to generate statically-linked musl binaries with specific toolchains
based on existing cc_binary targets
"""

def _musl_binary_impl(ctx):
    actual_target = ctx.attr.binary[0]

    return [
        DefaultInfo(
            files = actual_target[DefaultInfo].files,
            runfiles = actual_target[DefaultInfo].default_runfiles,
        ),
    ]

# buildifier: disable=unused-variable
def _make_musl_rule(target_platform_label, dynamic_toolchain_override):
    def _transition_impl(settings, attr):
        return {
            "//command_line_option:extra_toolchains": [dynamic_toolchain_override],
            "//command_line_option:platforms": [target_platform_label],
        }

    _trans = transition(
        implementation = _transition_impl,
        inputs = [],
        outputs = [
            "//command_line_option:platforms",
            "//command_line_option:extra_toolchains",
        ],
    )

    return rule(
        implementation = _musl_binary_impl,
        attrs = {
            "binary": attr.label(mandatory = True, cfg = _trans),
            "_allowlist_function_transition": attr.label(
                default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
            ),
        },
    )

musl_amd64_binary = _make_musl_rule(
    "//toolchain:platform_amd64_musl",
    "@zig_sdk//libc_aware/toolchain:linux_amd64_musl",
)

musl_arm64_binary = _make_musl_rule(
    "//toolchain:platform_arm64_musl",
    "@zig_sdk//libc_aware/toolchain:linux_arm64_musl",
)

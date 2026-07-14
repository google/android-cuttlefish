# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@aspect_rules_lint//format:defs.bzl", "format_test")
load("@cc_compatibility_proxy//:proxy.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_library.bzl", "sh_library")
load("//:build_variables.bzl", BUILD_VAR_COPTS = "COPTS", BUILD_VAR_LINKOPTS = "LINKOPTS")
load("//tools/lint:linters.bzl", "buildifier_test", "clang_tidy_test", "dwyu_rule", "shellcheck_test")

visibility(["//..."])

COPTS = BUILD_VAR_COPTS
LINKOPTS = BUILD_VAR_LINKOPTS

def _cf_build_test_implementation(name, srcs, **kwargs):
    native.filegroup(
        name = name + "_LINT_TEST_starlark_files",
        srcs = (srcs or []) + ["BUILD.bazel"],
        tags = ["lint-with-buildifier"],
    )
    buildifier_test(
        name = name + "_LINT_TEST",
        srcs = [":" + name + "_LINT_TEST_starlark_files"],
        **kwargs,
    )

cf_build_test = macro(
    inherit_attrs = "common",
    attrs = {
        "srcs": attr.label_list(
            configurable = False,
            default = [],
        )
    },
    implementation = _cf_build_test_implementation,
)

def _cf_cc_binary_implementation(name, clang_format_enabled, clang_tidy_enabled, copts, depend_on_what_you_use_enabled, linkopts, **kwargs):
    if not clang_tidy_enabled and not kwargs["deprecation"]:
        kwargs["deprecation"] = "Not covered by clang-tidy"
    cc_binary(
        name = name,
        copts = (copts or []) + COPTS,
        linkopts = (linkopts or []) + LINKOPTS,
        **kwargs,
    )
    if clang_format_enabled:
        format_test(
            name = name + "_format_test",
            cc = "//tools/format:clang_format",
            disable_git_attribute_checks = True,
            srcs = (kwargs.get("srcs") or []) + (kwargs.get("hdrs") or []),
            visibility = ["//visibility:private"],
        )
    if clang_tidy_enabled:
        clang_tidy_test(
            name = name + "_clang_tidy",
            srcs = [":" + name],
            tags = ["clang_tidy", "clang-tidy"],
            visibility = ["//visibility:private"],
        )
    if depend_on_what_you_use_enabled:
        dwyu_rule(
            name = name + "_depend_on_what_you_use",
            deps = [":" + name],
            testonly = True,
        )

cf_cc_binary = macro(
    inherit_attrs = cc_binary,
    attrs = {
        "clang_format_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding format_test target is generated"),
        "clang_tidy_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding clang_tidy_test target is generated"),
        "copts": attr.string_list(configurable = False, default = []),
        "depend_on_what_you_use_enabled": attr.bool(configurable = False, default = False, doc = "Decide if a corresponding depend-on-what-you-use target is generated"),
        "linkopts": attr.string_list(configurable = False, default = []),
    },
    implementation = _cf_cc_binary_implementation,
)

def _cf_cc_library_implementation(name, clang_format_enabled, clang_tidy_enabled, copts, depend_on_what_you_use_enabled, **kwargs):
    if not clang_tidy_enabled and not kwargs["deprecation"]:
        kwargs["deprecation"] = "Not covered by clang-tidy"
    cc_library(
        name = name,
        copts = (copts or []) + COPTS,
        **kwargs,
    )
    if clang_format_enabled:
        format_test(
            name = name + "_format_test",
            cc = "//tools/format:clang_format",
            disable_git_attribute_checks = True,
            srcs = (kwargs.get("srcs") or []) + (kwargs.get("hdrs") or []),
            visibility = ["//visibility:private"],
        )
    if clang_tidy_enabled:
        clang_tidy_test(
            name = name + "_clang_tidy",
            srcs = [":" + name],
            tags = ["clang_tidy", "clang-tidy"],
            visibility = ["//visibility:private"],
        )
    if depend_on_what_you_use_enabled:
        dwyu_rule(
            name = name + "_depend_on_what_you_use",
            deps = [":" + name],
            testonly = True,
        )

cf_cc_library = macro(
    inherit_attrs = cc_library,
    attrs = {
        "clang_format_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding format_test target is generated"),
        "clang_tidy_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding clang_tidy_test target is generated"),
        "copts": attr.string_list(configurable = False, default = []),
        "depend_on_what_you_use_enabled": attr.bool(configurable = False, default = False, doc = "Decide if a corresponding depend-on-what-you-use target is generated"),
    },
    implementation = _cf_cc_library_implementation,
)

def _cf_cc_test_implementation(name, clang_format_enabled, clang_tidy_enabled, copts, depend_on_what_you_use_enabled, deps, **kwargs):
    if not clang_tidy_enabled and not kwargs["deprecation"]:
        kwargs["deprecation"] = "Not covered by clang-tidy"
    cc_test(
        name = name,
        copts = (copts or []) + COPTS,
        deps = deps + [
            "@googletest//:gtest",
            "@googletest//:gtest_main",
        ],
        **kwargs,
    )
    if clang_format_enabled:
        format_test(
            name = name + "_format_test",
            cc = "//tools/format:clang_format",
            disable_git_attribute_checks = True,
            srcs = (kwargs.get("srcs") or []) + (kwargs.get("hdrs") or []),
            visibility = ["//visibility:private"],
        )
    if clang_tidy_enabled:
        clang_tidy_test(
            name = name + "_clang_tidy",
            srcs = [":" + name],
            tags = ["clang_tidy", "clang-tidy"],
            visibility = ["//visibility:private"],
        )
    if depend_on_what_you_use_enabled:
        dwyu_rule(
            name = name + "_depend_on_what_you_use",
            deps = [":" + name],
            testonly = True,
        )

cf_cc_test = macro(
    inherit_attrs = cc_test,
    attrs = {
        "clang_format_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding format_test target is generated"),
        "clang_tidy_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding clang_tidy_test target is generated"),
        "copts": attr.string_list(configurable = False, default = []),
        "depend_on_what_you_use_enabled": attr.bool(configurable = False, default = False, doc = "Decide if a corresponding depend-on-what-you-use target is generated"),
        "deps": attr.label_list(configurable = False),
    },
    implementation = _cf_cc_test_implementation,
)

def _cf_sh_binary_implementation(name, shellcheck_enabled, **kwargs):
    sh_binary(
        name = name,
        **kwargs,
    )
    if shellcheck_enabled:
        shellcheck_test(
            name = name + "_shellcheck",
            srcs = [":" + name],
            tags = ["shellcheck"],
            visibility = ["//visibility:private"],
        )

cf_sh_binary = macro(
    inherit_attrs = sh_binary,
    attrs = {
        "shellcheck_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding shellcheck_test target is generated"),
    },
    implementation = _cf_sh_binary_implementation,
)

def _cf_sh_library_implementation(name, shellcheck_enabled, **kwargs):
    sh_library(
        name = name,
        **kwargs,
    )
    if shellcheck_enabled:
        shellcheck_test(
            name = name + "_shellcheck",
            srcs = [":" + name],
            tags = ["shellcheck"],
            visibility = ["//visibility:private"],
        )

cf_sh_library = macro(
    inherit_attrs = sh_library,
    attrs = {
        "shellcheck_enabled": attr.bool(configurable = False, default = True, doc = "Decide if a corresponding shellcheck_test target is generated"),
    },
    implementation = _cf_sh_library_implementation,
)

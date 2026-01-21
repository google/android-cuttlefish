# load(":cc_toolchain_config.bzl", "linux_local_clang_toolchain_config")

# Relevant documentation:
# - https://bazel.build/extending/macros
# - https://bazel.build/extending/toolchains
# - https://bazel.build/reference/be/platforms-and-toolchains#toolchain
# - https://bazel.build/tutorials/ccp-toolchain-config
# def _linux_local_clang_impl(name, visibility, exec_compatible_with, version, **kwargs):
#     linux_local_clang_toolchain_config(
#         name = name + "_config",
#         visibility = ["//visibility:private"],
#         version = version,
#     )
#     native.cc_toolchain(
#         name = name + "_cc_toolchain",
#         visibility = ["//visibility:private"],
#         toolchain_identifier = name,
#         toolchain_config = ":" + name + "_config",
#         all_files = ":empty",
#         compiler_files = ":empty",
#         dwp_files = ":empty",
#         linker_files = ":empty",
#         objcopy_files = ":empty",
#         strip_files = ":empty",
#         supports_param_files = 0,
#     )
#     native.toolchain(
#         name = name,
#         visibility = visibility,
#         toolchain = ":" + name + "_cc_toolchain",
#         toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
#         exec_compatible_with = exec_compatible_with,
#         target_compatible_with = ["@platforms//os:linux"],
#         **kwargs,
#     )

# Defines a compiler toolchain that calls /usr/bin/clang-{version} to compile and link c++.
#
# Relevant documentation:
# - https://bazel.build/rules/lib/globals/bzl.html#macro
# - https://bazel.build/rules/lib/toplevel/attr#int
# - https://bazel.build/rules/lib/toplevel/attr#label_list
# linux_local_clang = macro(
#     attrs = {
#         "exec_compatible_with": attr.label_list(configurable = False),
#         "version": attr.int(configurable = False, mandatory = True),
#     },
#     implementation = _linux_local_clang_impl,
# )

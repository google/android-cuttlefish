# Relevant documentation:
# - https://bazel.build/reference/be/platforms-and-toolchains
# - https://bazel.build/rules/lib/builtins/path.html
# - https://bazel.build/rules/lib/builtins/repository_ctx
# - https://bazel.build/rules/lib/globals/bzl.html#repository_rule
def _file_detector_impl(repository_ctx):
    root_build_file = 'package(default_visibility = ["//visibility:public"])\n\n'
    for (filename, constraint_setting) in repository_ctx.attr.files.items():
        file_path = repository_ctx.path(filename)
        repository_ctx.watch(file_path)
        if file_path.exists:
            exists_str = "present"
        else:
            exists_str = "absent"
        build_fragment = """
constraint_setting(
    name = "{0}",
    default_constraint_value = ":{0}_{1}"
)
constraint_value(
    name = "{0}_present",
    constraint_setting = ":{0}",
)
constraint_value(
    name = "{0}_absent",
    constraint_setting = ":{0}",
)
        """.strip().format(constraint_setting, exists_str)
        root_build_file += "\n" + build_fragment + "\n"
    build_file = repository_ctx.path("BUILD.bazel")
    repository_ctx.file(build_file, root_build_file, False)

# Creates a repository that defines `constraint_setting`s and
# `constraint_value`s based on the existence of some files on the host.
#
# Example usage:
# ```
# file_detector = use_repo_rule("//toolchain:file_detector.bzl", "file_detector")
#
# file_detector(
#     name = "clang_detector",
#     files = {
#         "/usr/bin/clang-11": "clang_11",
#     },
# ),
# ```
# This expands to a repository `@clang_detector` defining the following
# targets:
# ```
# constraint_setting(
#     name = "clang_11",
#     default_constraint_value = ":clang_11_present",
# )
# constraint_value(
#     name = "clang_11_present",
#     constraint_setting = ":clang_11",
# )
# constraint_value(
#     name = "clang_11_absent",
#     constraint_setting = ":clang_11",
# )
# ```
# The default constraint value of the "@clang_detector//:clang_11" setting is
# set to either "@clang_detector//:clang_11_present" or
# "@clang_detector//:clang_11_absent" depending on whether the file
# /usr/bin/clang-11 is present on the host.
#
# This is helpful for toolchain resolution. A toolchain relying on host
# executables can reference these constraints in its `exec_compatible_with`
# list, which is used to report availability.
#
# Relevant documentation:
# - https://bazel.build/extending/toolchains
# - https://bazel.build/reference/be/platforms-and-toolchains
# - https://bazel.build/rules/lib/globals/bzl.html#repository_rule
# - https://bazel.build/rules/lib/toplevel/attr#string_dict
file_detector = repository_rule(
    implementation = _file_detector_impl,
    attrs = {
        "files": attr.string_dict(
            allow_empty = False,
            configurable = False,
            mandatory = True,
        ),
    },
)

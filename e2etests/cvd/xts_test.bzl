def _cvd_cts_test_impl(ctx):
    # Per https://bazel.build/extending/rules#executable_rules_and_test_rules,
    # test rules must produce an executable. This rule effectively populates
    # a shell script with arguments that then later runs the underlying test
    # runner when invoked by bazel:
    test_wrapper_template = """#!/bin/sh
set -x
{xts_runner} \
 --default_build_branch={cuttlefish_branch} \
 --default_build_target={cuttlefish_target} \
 --test_suite_build_branch={cts_branch} \
 --test_suite_build_target={cts_target} \
 --xts_type=cts \
 --xts_args={cts_args}
"""
    test_wrapper_content = test_wrapper_template.format(
        xts_runner = ctx.attr._xts_runner.files_to_run.executable.short_path,
        cuttlefish_branch = ctx.attr.cuttlefish_branch,
        cuttlefish_target = ctx.attr.cuttlefish_target,
        cts_branch = ctx.attr.cts_branch,
        cts_target = ctx.attr.cts_target,
        cts_args = "\"" + " ".join(ctx.attr.cts_args) + "\"",
    )
    test_wrapper_shfile = ctx.actions.declare_file(ctx.attr.name + ".sh")
    ctx.actions.write(test_wrapper_shfile, test_wrapper_content, is_executable = True)

    return [
        DefaultInfo(
            executable = test_wrapper_shfile,
            runfiles = ctx.runfiles(
                transitive_files = ctx.attr._xts_runner.files,
            ),
        ),
    ]

cvd_cts_test = rule(
    _cvd_cts_test_impl,
    attrs = {
        "cuttlefish_branch": attr.string(
            doc = "The branch of the Cuttlefish instance to fetch.",
            mandatory = True,
        ),
        "cuttlefish_target": attr.string(
            doc = "The target of the Cuttlefish instance to fetch.",
            mandatory = True,
        ),
        "cuttlefish_create_args": attr.string_list(
            doc = "The additional args passed to the `cvd create` command.",
            mandatory = True,
        ),
        "cts_branch": attr.string(
            doc = "The target of the CTS suite to fetch.",
            mandatory = True,
        ),
        "cts_target": attr.string(
            doc = "The target of the CTS suite to fetch.",
            mandatory = True,
        ),
        "cts_args": attr.string_list(
            doc = "The additional args passed to the `cts-tradefed` command.",
            mandatory = True,
        ),
        "_xts_runner": attr.label(
            cfg = config.exec(exec_group = "test"),
            default = "//cvd:cvd_xts_test_runner",
            executable = True,
        ),
    },
    doc = "Fetches a Cuttlefish build, fetchs a CTS build, launchs a Cuttlefish instance, and then CTS against the instance.",
    test = True,
)

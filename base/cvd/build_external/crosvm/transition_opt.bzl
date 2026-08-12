def _file_from_label(l):
    files = l.files.to_list()
    if len(files) != 1:
        fail(msg = "Unexpected number of files in target {}: {}".format(l, len(files)))
    return files[0]

def _build_in_opt_transition_impl(settings, attr):
    return {"//command_line_option:compilation_mode": "opt"}

# https://bazel.build/rules/lib/builtins/transition#transition
build_in_opt_transition = transition(
    implementation = _build_in_opt_transition_impl,
    inputs = [],
    outputs = ["//command_line_option:compilation_mode"],
)

def _build_in_opt_rule_impl(ctx):
    input_file = _file_from_label(ctx.attr.actual)
    output = ctx.actions.declare_file(ctx.attr.name)
    ctx.actions.run_shell(
        mnemonic = "CopyOutput",
        inputs = [input_file],
        outputs = [output],
        command = " ".join(["cp", input_file.path, output.path])
    )
    return [
        DefaultInfo(
            executable = output,
            files = depset([output])
        ),
    ]

# https://bazel.build/extending/config#attaching-transitions
build_in_opt = rule(
    attrs = {
        "actual": attr.label(),
    },
    implementation = _build_in_opt_rule_impl,
    cfg = build_in_opt_transition,
    executable = True,
)

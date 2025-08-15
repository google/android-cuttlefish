"""Collect all source files in a directory."""

def _collect_source_files_impl(ctx):
    out = ctx.actions.declare_directory(ctx.attr.out)
    ctx.actions.run_shell(
        command = "cp  -t {out} -r {srcs}".format(
            srcs = " ".join([f.path for f in ctx.files.srcs]),
            out = out.path,
        ),
        inputs = ctx.files.srcs,
        outputs = [out],
    )
    return DefaultInfo(files = depset([out]))

collect_files_in_dir = rule(
    doc = "Collect all source files in a directory.",
    implementation = _collect_source_files_impl,
    attrs = {
        "out": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = True),
    },
)

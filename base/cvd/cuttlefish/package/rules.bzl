def _file_from_label(l):
    files = l.files.to_list()
    if len(files) != 1:
        fail(msg = "Unexpected number of files in target {}: {}".format(l, len(files)))
    return files[0]

def _package_files_impl(ctx):
    default_outputs = list()
    for (dst, src) in ctx.attr.dst_to_src_mapping.items():
        out_file = ctx.actions.declare_file(dst)
        if dst == ctx.attr.executable:
            executable = out_file
        default_outputs.append(out_file)

        input_file = _file_from_label(src)
        ctx.actions.run_shell(
            outputs = [out_file],
            inputs = [input_file],
            command = "mkdir -p " + out_file.dirname + " && cp " + input_file.path + " " + out_file.path,
        )
    return [
        DefaultInfo(
            executable = executable,
            files = depset(default_outputs),
        ),
    ]

package_files = rule(
    attrs = {
        "executable": attr.string(),
        "dst_to_src_mapping": attr.string_keyed_label_dict(allow_files = True),
    },
    executable = True,
    implementation = _package_files_impl,
)

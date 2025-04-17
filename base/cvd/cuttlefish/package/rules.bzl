def _file_from_label_keyed_string_dict_key(k):
    # NB: The Targets in a label_keyed_string_dict attribute have the key's
    # source file in a depset, as opposed being represented directly as in a
    # label_list attribute.
    files = k.files.to_list()
    if len(files) != 1:
        fail(msg = "Unexpected number of files in target {}: {}".format(k, len(files)))
    return files[0]

def _package_files_impl(ctx):
    default_outputs = list()
    for (src, dest) in ctx.attr.src_mapping.items():
        out_file = ctx.actions.declare_file(dest)
        if dest == ctx.attr.executable:
            executable = out_file
        default_outputs.append(out_file)

        input_file = _file_from_label_keyed_string_dict_key(src)
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
        "src_mapping": attr.label_keyed_string_dict(allow_files = True),
    },
    executable = True,
    implementation = _package_files_impl,
)
